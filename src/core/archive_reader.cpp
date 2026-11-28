#include <ctar/reader.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "block_store.h"
#include "compress.h"
#include "file_format.h"
#include "file_handle.h"
#include "options.h"
#include "src/utils/status.h"
#include "xxhash.h"

namespace ctar
{

namespace
{
    Error MakeError(const ErrorCode code, std::string message) { return {code, std::move(message)}; }

    ErrorCode MapErrorType(const ErrorType type) noexcept
    {
        switch (type)
        {
        case ErrorType::kNotFound:
            return ErrorCode::kNotFound;
        case ErrorType::kCorruption:
            return ErrorCode::kCorruption;
        case ErrorType::kNotSupported:
            return ErrorCode::kUnsupported;
        case ErrorType::kInvalidArgument:
            return ErrorCode::kInvalidArgument;
        case ErrorType::kIoError:
            return ErrorCode::kIo;
        case ErrorType::kRuntimeError:
            return ErrorCode::kRuntimeError;
        case ErrorType::kSuccess:
            break;
        }
        return ErrorCode::kRuntimeError;
    }

    Error FromStatus(const ErrorStatus& status) { return {MapErrorType(status.type()), status.err_message}; }

    /// Normalizes a virtual archive path: leading slash optional, empty
    /// components collapsed, "." and ".." rejected.
    Result<std::string> NormalizePath(const std::string_view path)
    {
        std::string normalized;
        normalized.reserve(path.size() + 1);
        size_t start = 0;
        while (start < path.size())
        {
            const size_t end = path.find('/', start);
            const size_t stop = end == std::string_view::npos ? path.size() : end;
            const std::string_view part = path.substr(start, stop - start);
            if (!part.empty())
            {
                if (part == "." || part == "..")
                {
                    return std::unexpected(
                        MakeError(ErrorCode::kInvalidArgument, std::format("invalid path '{}'", path)));
                }
                normalized.push_back('/');
                normalized.append(part);
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1;
        }
        if (normalized.empty())
        {
            normalized = "/";
        }
        return normalized;
    }

    std::string JoinPath(const std::string_view parent, const std::string_view name)
    {
        if (parent == "/")
        {
            return std::format("/{}", name);
        }
        return std::format("{}/{}", parent, name);
    }

    /// A stored file name must be exactly one path component. The archive is
    /// untrusted: names containing separators or "."/".." would otherwise
    /// escape the virtual root or change the meaning of Entry::path.
    bool IsValidPathComponent(const std::string_view name)
    {
        return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos;
    }
} // namespace

struct ArchiveReader::Impl
{
    struct Resolved
    {
        entry_id_t id = 0;
        bool is_dir = false;
        std::string path;
    };

    BlockStore store;
    ReaderOptions options;
    std::unique_ptr<ctar::ReadFile> read_file;
    std::string root_stored_path;
    // virtual directory path -> dir id (root is "/")
    std::unordered_map<std::string, entry_id_t> dir_path_to_id;
    // dir id -> virtual directory path
    std::unordered_map<entry_id_t, std::string> dir_id_to_path;
    // dir id -> (file name -> file id); the name views point into the
    // PackFileHandle buffers, which stay stable after deserialization.
    std::unordered_map<entry_id_t, std::unordered_map<std::string_view, entry_id_t>> file_name_to_id;
    // block id -> validated block metadata
    std::unordered_map<uint64_t, const DataBlockMeta*> block_by_id;

    Result<void> BuildIndexes()
    {
        const auto& handle = store.GetPackFileHandle();
        const auto root_path = handle.GetDirPathByDirId(kRootDirId);
        if (!root_path)
        {
            return std::unexpected(FromStatus(root_path.error()));
        }
        root_stored_path = std::string(root_path.value());
        dir_path_to_id.emplace("/", kRootDirId);
        dir_id_to_path.emplace(kRootDirId, "/");

        const fs::path root_fs = fs::path(root_stored_path).lexically_normal();
        for (const auto& dir_entry : handle.GetDirEntryRecords())
        {
            const entry_id_t dir_id = dir_entry.first;
            if (dir_id == kRootDirId)
            {
                continue;
            }
            const auto stored = handle.GetDirPathByDirId(dir_id);
            if (!stored)
            {
                return std::unexpected(FromStatus(stored.error()));
            }
            const fs::path relative = fs::path(stored.value()).lexically_normal().lexically_relative(root_fs);
            if (relative.is_absolute())
            {
                return std::unexpected(
                    MakeError(ErrorCode::kCorruption,
                              std::format("directory path '{}' is outside the archive root", stored.value())));
            }
            for (const auto& component : relative)
            {
                if (component == "." || component == "..")
                {
                    return std::unexpected(
                        MakeError(ErrorCode::kCorruption,
                                  std::format("directory path '{}' escapes the archive root", stored.value())));
                }
            }
            std::string virtual_path;
            if (relative.empty() || relative == ".")
            {
                virtual_path = "/";
            }
            else
            {
                virtual_path = std::format("/{}", relative.generic_string());
            }
            if (!dir_path_to_id.emplace(virtual_path, dir_id).second)
            {
                return std::unexpected(
                    MakeError(ErrorCode::kCorruption, std::format("duplicate directory path '{}'", virtual_path)));
            }
            dir_id_to_path.emplace(dir_id, std::move(virtual_path));
        }

        for (const auto& dir_entry : handle.GetDirIdToChildFileIds())
        {
            auto& names = file_name_to_id[dir_entry.first];
            for (const entry_id_t file_id : dir_entry.second)
            {
                const auto name = handle.GetFileNameByFileId(file_id);
                if (!name)
                {
                    return std::unexpected(FromStatus(name.error()));
                }
                if (!IsValidPathComponent(name.value()))
                {
                    return std::unexpected(
                        MakeError(ErrorCode::kCorruption, std::format("invalid file name '{}'", name.value())));
                }
                if (!names.emplace(name.value(), file_id).second)
                {
                    return std::unexpected(
                        MakeError(ErrorCode::kCorruption, std::format("duplicate file name '{}'", name.value())));
                }
            }
        }

        // A path must not be both a file and a directory; otherwise Resolve
        // would silently shadow one of them.
        for (const auto& dir_entry : file_name_to_id)
        {
            const auto dir_path = dir_id_to_path.find(dir_entry.first);
            if (dir_path == dir_id_to_path.end())
            {
                return std::unexpected(MakeError(
                    ErrorCode::kCorruption, std::format("directory {} has no path", dir_entry.first)));
            }
            for (const auto& file_entry : dir_entry.second)
            {
                const std::string full_path = JoinPath(dir_path->second, file_entry.first);
                if (dir_path_to_id.contains(full_path))
                {
                    return std::unexpected(MakeError(
                        ErrorCode::kCorruption,
                        std::format("path '{}' is both a file and a directory", full_path)));
                }
            }
        }

        for (const auto& block : store.GetDataBlockMetas())
        {
            block_by_id.emplace(block.block_id, &block);
        }
        return {};
    }

    Result<Resolved> Resolve(const std::string_view raw_path) const
    {
        auto normalized = NormalizePath(raw_path);
        if (!normalized)
        {
            return std::unexpected(normalized.error());
        }
        const std::string& path = normalized.value();
        if (const auto it = dir_path_to_id.find(path); it != dir_path_to_id.end())
        {
            return Resolved{it->second, true, path};
        }
        const size_t sep = path.rfind('/');
        const std::string parent = sep == 0 ? "/" : path.substr(0, sep);
        const std::string_view name(path.data() + sep + 1, path.size() - sep - 1);
        const auto parent_it = dir_path_to_id.find(parent);
        if (parent_it == dir_path_to_id.end())
        {
            return std::unexpected(MakeError(ErrorCode::kNotFound, std::format("no such path '{}'", path)));
        }
        const auto dir_files = file_name_to_id.find(parent_it->second);
        if (dir_files == file_name_to_id.end())
        {
            return std::unexpected(MakeError(ErrorCode::kNotFound, std::format("no such path '{}'", path)));
        }
        const auto file_it = dir_files->second.find(name);
        if (file_it == dir_files->second.end())
        {
            return std::unexpected(MakeError(ErrorCode::kNotFound, std::format("no such path '{}'", path)));
        }
        return Resolved{file_it->second, false, path};
    }

    Result<Entry> MakeEntry(const entry_id_t id, const bool is_dir, std::string path) const
    {
        const auto& handle = store.GetPackFileHandle();
        const auto& records = is_dir ? handle.GetDirEntryRecords() : handle.GetFileEntryRecords();
        const auto record = records.find(id);
        if (record == records.end())
        {
            return std::unexpected(
                MakeError(ErrorCode::kCorruption, std::format("entry {} has no metadata record", id)));
        }
        const EntryMeta& meta = record->second.entry_meta;
        Entry entry;
        entry.path = std::move(path);
        entry.kind = is_dir ? EntryKind::kDirectory : EntryKind::kFile;
        entry.size = is_dir ? 0 : meta.file_orig_size;
        entry.mtime_ms = meta.modify_time;
        entry.permissions = meta.permissions;
        entry.owner.assign(meta.owner_name, strnlen(meta.owner_name, sizeof(meta.owner_name)));
        entry.group.assign(meta.group_name, strnlen(meta.group_name, sizeof(meta.group_name)));
        return entry;
    }

    Result<std::shared_ptr<const Buffer>> ReadFileById(const entry_id_t file_id) const
    {
        const auto& handle = store.GetPackFileHandle();
        const auto& records = handle.GetFileEntryRecords();
        const auto record = records.find(file_id);
        if (record == records.end())
        {
            return std::unexpected(
                MakeError(ErrorCode::kCorruption, std::format("file {} has no metadata record", file_id)));
        }
        const EntryMeta& entry = record->second.entry_meta;

        const auto block_it = block_by_id.find(entry.block_id);
        if (block_it == block_by_id.end())
        {
            return std::unexpected(
                MakeError(ErrorCode::kCorruption, std::format("file {} names unknown block {}", file_id, entry.block_id)));
        }
        const DataBlockMeta& block = *block_it->second;
        const auto file_it = std::lower_bound(block.file_ids.begin(), block.file_ids.end(), file_id);
        if (file_it == block.file_ids.end() || *file_it != file_id)
        {
            return std::unexpected(
                MakeError(ErrorCode::kCorruption, std::format("block {} does not contain file {}", block.block_id, file_id)));
        }
        const size_t index = static_cast<size_t>(file_it - block.file_ids.begin());
        const uint64_t original_size = block.file_orig_sizes[index];
        const uint64_t stored_size = block.file_compressed_sizes[index];
        const CompressionType compression_type = block.file_compressed_types[index];

        if (options.max_file_size > 0 &&
            (original_size > options.max_file_size || stored_size > options.max_file_size))
        {
            return std::unexpected(MakeError(
                ErrorCode::kTooLarge,
                std::format("file is {} bytes ({} stored), above the configured limit of {} bytes", original_size,
                            stored_size, options.max_file_size)));
        }

        std::unique_ptr<char[]> stored;
        if (stored_size > 0)
        {
            stored = std::make_unique_for_overwrite<char[]>(static_cast<size_t>(stored_size));
            const auto read_status =
                read_file->RandomRead(entry.file_offset, static_cast<size_t>(stored_size), stored.get());
            if (!read_status || read_status.value() != stored_size)
            {
                return std::unexpected(MakeError(ErrorCode::kIo, "failed to read file data from the archive"));
            }
        }

        std::unique_ptr<char[]> data;
        uint64_t hash = kZeroXXHash;
        if (original_size > 0)
        {
            if (compression_type == CompressionType::kNone)
            {
                data = std::move(stored);
                hash = XXH64(data.get(), original_size, kHashSeed);
            }
            else
            {
                data = std::make_unique_for_overwrite<char[]>(static_cast<size_t>(original_size));
                const uint64_t decompressed_size = compress::Decompress(compression_type, stored.get(), data.get(),
                                                                        stored_size, original_size);
                if (decompressed_size != original_size)
                {
                    return std::unexpected(MakeError(ErrorCode::kCorruption, "failed to decompress file data"));
                }
                stored.reset();
                hash = XXH64(data.get(), original_size, kHashSeed);
            }
        }
        if (hash != block.file_hashes[index])
        {
            return std::unexpected(
                MakeError(ErrorCode::kCorruption, "file data does not match the recorded hash"));
        }
        return ArchiveReader::MakeBuffer(std::move(data), static_cast<size_t>(original_size));
    }
};

Buffer::Buffer(std::unique_ptr<char[]> data, const size_t size) noexcept : data_(std::move(data)), size_(size) {}

std::shared_ptr<const Buffer> ArchiveReader::MakeBuffer(std::unique_ptr<char[]> data, const size_t size)
{
    return std::shared_ptr<const Buffer>(new Buffer(std::move(data), size));
}

File::File(std::shared_ptr<const Buffer> data) noexcept : data_(std::move(data)) {}

uint64_t File::Size() const noexcept { return data_ ? data_->size() : 0; }

Result<size_t> File::Read(const std::span<char> dst)
{
    auto count = ReadAt(offset_, dst);
    if (count)
    {
        offset_ += count.value();
    }
    return count;
}

Result<size_t> File::ReadAt(const uint64_t offset, const std::span<char> dst) const
{
    const uint64_t size = Size();
    if (offset > size)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, "offset is past the end of the file"));
    }
    const size_t available = static_cast<size_t>(size - offset);
    const size_t count = std::min(dst.size(), available);
    if (count > 0)
    {
        std::memcpy(dst.data(), data_->data() + static_cast<size_t>(offset), count);
    }
    return count;
}

std::shared_ptr<const Buffer> File::ReadAll() const { return data_; }

ArchiveReader::ArchiveReader() noexcept = default;
ArchiveReader::~ArchiveReader() = default;
ArchiveReader::ArchiveReader(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ArchiveReader::ArchiveReader(ArchiveReader&&) noexcept = default;
ArchiveReader& ArchiveReader::operator=(ArchiveReader&&) noexcept = default;

Result<ArchiveReader> ArchiveReader::Open(std::string archive_path, const ReaderOptions options)
{
    auto impl = std::make_unique<Impl>();
    impl->options = options;
    impl->store.SetMaxMetadataSize(options.max_metadata_size);
    // The per-file budget is enforced per read (kTooLarge) so a legitimate file
    // above the limit does not make the whole archive unreadable. Metadata
    // validation still checks the structural LZ4 bounds with no size budget.
    if (auto status = impl->store.RestoreBlocksFromPack(archive_path, 0); !status)
    {
        return std::unexpected(FromStatus(status.error()));
    }
    auto read_file = NewPosixReadFile(archive_path);
    if (!read_file)
    {
        return std::unexpected(FromStatus(read_file.error()));
    }
    impl->read_file = std::move(read_file.value());
    if (auto built = impl->BuildIndexes(); !built)
    {
        return std::unexpected(built.error());
    }
    return ArchiveReader(std::move(impl));
}

Result<std::vector<Entry>> ArchiveReader::List(const std::string_view dir, const bool recursive) const
{
    if (!impl_)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, "reader is not open"));
    }
    auto resolved = impl_->Resolve(dir);
    if (!resolved)
    {
        return std::unexpected(resolved.error());
    }
    if (!resolved->is_dir)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, std::format("'{}' is not a directory", dir)));
    }

    const auto& handle = impl_->store.GetPackFileHandle();
    std::vector<Entry> entries;
    std::vector<entry_id_t> queue{resolved->id};
    for (size_t head = 0; head < queue.size(); ++head)
    {
        const entry_id_t dir_id = queue[head];
        const auto dir_path_it = impl_->dir_id_to_path.find(dir_id);
        if (dir_path_it == impl_->dir_id_to_path.end())
        {
            return std::unexpected(
                MakeError(ErrorCode::kCorruption, std::format("directory {} has no path", dir_id)));
        }
        const std::string& dir_path = dir_path_it->second;

        for (const entry_id_t child_id : handle.GetChildDirIdsByDirId(dir_id))
        {
            const auto child_path = impl_->dir_id_to_path.find(child_id);
            if (child_path == impl_->dir_id_to_path.end())
            {
                return std::unexpected(
                    MakeError(ErrorCode::kCorruption, std::format("directory {} has no path", child_id)));
            }
            auto entry = impl_->MakeEntry(child_id, true, child_path->second);
            if (!entry)
            {
                return std::unexpected(entry.error());
            }
            entries.push_back(std::move(entry.value()));
            if (recursive)
            {
                queue.push_back(child_id);
            }
        }
        for (const entry_id_t child_id : handle.GetChildFileIdsByDirId(dir_id))
        {
            const auto name = handle.GetFileNameByFileId(child_id);
            if (!name)
            {
                return std::unexpected(FromStatus(name.error()));
            }
            auto entry = impl_->MakeEntry(child_id, false, JoinPath(dir_path, name.value()));
            if (!entry)
            {
                return std::unexpected(entry.error());
            }
            entries.push_back(std::move(entry.value()));
        }
    }
    std::ranges::sort(entries, {}, &Entry::path);
    return entries;
}

Result<Entry> ArchiveReader::Stat(const std::string_view path) const
{
    if (!impl_)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, "reader is not open"));
    }
    auto resolved = impl_->Resolve(path);
    if (!resolved)
    {
        return std::unexpected(resolved.error());
    }
    return impl_->MakeEntry(resolved->id, resolved->is_dir, std::move(resolved->path));
}

Result<File> ArchiveReader::OpenFile(const std::string_view path) const
{
    if (!impl_)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, "reader is not open"));
    }
    auto resolved = impl_->Resolve(path);
    if (!resolved)
    {
        return std::unexpected(resolved.error());
    }
    if (resolved->is_dir)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, std::format("'{}' is a directory", path)));
    }
    auto data = impl_->ReadFileById(resolved->id);
    if (!data)
    {
        return std::unexpected(data.error());
    }
    return File(std::move(data.value()));
}

Result<std::shared_ptr<const Buffer>> ArchiveReader::ReadFile(const std::string_view path) const
{
    if (!impl_)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, "reader is not open"));
    }
    auto resolved = impl_->Resolve(path);
    if (!resolved)
    {
        return std::unexpected(resolved.error());
    }
    if (resolved->is_dir)
    {
        return std::unexpected(MakeError(ErrorCode::kInvalidArgument, std::format("'{}' is a directory", path)));
    }
    return impl_->ReadFileById(resolved->id);
}

} // namespace ctar
