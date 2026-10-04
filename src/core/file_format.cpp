#include <grp.h>
#include <pwd.h>
#include <ranges>
#include <sys/stat.h>

#include "file_format.h"
#include "src/utils/logger.h"
#include "src/utils/tools.h"
namespace ctar
{
    namespace
    {
        constexpr std::vector<entry_id_t> empty_vec;
    }


    void EntryMeta::FormatOutput(std::ostream& os, const DirFileRef& dir_file_ref, const OutputMode mode,
                                 const bool human_readable, const ListColWidths& col_widths) const
    {
        const auto it = std::ostreambuf_iterator<char>(os);

        std::string_view owner(owner_name, strnlen(owner_name, sizeof(owner_name)));
        std::string_view group(group_name, strnlen(group_name, sizeof(group_name)));
        constexpr long long ms_div = 1000;
        std::string time_str = FormatTime(modify_time / ms_div);
        std::string format_file_size;
        if (human_readable)
        {
            format_file_size = HumanReadableBytes(file_orig_size);
        }
        else
        {
            format_file_size = std::to_string(file_orig_size);
        }

        if (mode == OutputMode::LIST)
        {
            std::string perm;
            perm.reserve(10);
            perm += entry_type == EntryType::DIRECTORY ? 'd' : '-';
            perm += PermBitsToString(permissions);
            static_assert(kListColumnSize == 6, "LIST column count mismatch, update kListColumnSize");
            std::format_to(it, "{:10}  {:<{}}  {:<{}}  {:>{}}  {:<12}  {}/{}\n", perm, owner,
                           col_widths.owner_name_width, group, col_widths.group_name_width, format_file_size,
                           col_widths.file_size_width, time_str, dir_file_ref.dir_name, dir_file_ref.file_name);
        }
        else
        {
            // block_id,dir_id,file_id,permissions,owner,group,modify_time,offset,original_size,compressed_size,dir_name,file_name
            static_assert(kTsvColumnSize == 12, "TSV column count mismatch, update kTsvColumnSize");
            std::format_to(it, "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", block_id, dir_file_ref.dir_id,
                           dir_file_ref.file_id, permissions, owner, group, modify_time, offset, file_orig_size,
                           file_compressed_size, dir_file_ref.dir_name, dir_file_ref.file_name);
        }
    }


    void PackFileHeader::Serialize(std::string* dst) const
    {
        assert(file_meta_size >= 4 * unit::KiB - sizeof(PackFileHeader));
        PutFixed32(dst, magic_number);
        PutFixed32(dst, version.Encode());
        PutFixed64(dst, file_meta_size);
    }

    Status PackFileHeader::Deserialize(const char*& ptr)
    {
        magic_number = DecodeFixed32(ptr);
        ptr += sizeof(uint32_t);
        if (magic_number != MagicNumber)
        {
            return error_corruption("Invalid magic number");
        }
        const uint32_t version_number = DecodeFixed32(ptr);
        version = Version::Decode(version_number);
        ptr += sizeof(uint32_t);
        file_meta_size = DecodeFixed64(ptr);
        return {};
    }

    size_t PackFileHeader::SerializedSize() const { return FixedFieldTotalSize(magic_number, version, file_meta_size); }


    size_t DataBlockMeta::SerializedSize() const
    {
        return FixedFieldTotalSize(block_id, dir_id, block_file_offset, payload_size, block_size, hash_value) +
            ContainerSerializedTotalSize(file_ids, file_orig_sizes, file_compressed_sizes, file_compressed_types,
                                         file_offsets, file_hashes);
    }

    void DataBlockMeta::Serialize(std::string* dst) const
    {
        PutFixed64(dst, block_id);
        PutFixed64(dst, dir_id);
        PutFixed64(dst, block_file_offset);
        PutFixed64(dst, payload_size);
        PutFixed64(dst, block_size);
        PutFixed64(dst, hash_value);

        /* file_ids */
        PutFixed64(dst, file_ids.size());
        if (!file_ids.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_ids.data());
            dst->append(raw, file_ids.size() * sizeof(entry_id_t));
        }

        /* file_orig_sizes */
        PutFixed64(dst, file_orig_sizes.size());
        if (!file_orig_sizes.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_orig_sizes.data());
            dst->append(raw, file_orig_sizes.size() * sizeof(uint64_t));
        }

        /* file_compressed_sizes */
        PutFixed64(dst, file_compressed_sizes.size());
        if (!file_compressed_sizes.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_compressed_sizes.data());
            dst->append(raw, file_compressed_sizes.size() * sizeof(uint64_t));
        }

        /* file_compressed_types */
        PutFixed64(dst, file_compressed_types.size());
        for (CompressionType type : file_compressed_types)
        {
            PutFixed8(dst, static_cast<uint8_t>(type));
        }

        /* file_offsets */
        PutFixed64(dst, file_offsets.size());
        if (!file_offsets.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_offsets.data());
            dst->append(raw, file_offsets.size() * sizeof(uint64_t));
        }

        /* file_hashes */
        PutFixed64(dst, file_hashes.size());
        if (!file_hashes.empty())
        {
            const char* raw = reinterpret_cast<const char*>(file_hashes.data());
            dst->append(raw, file_hashes.size() * sizeof(uint64_t));
        }
    }

    Status DataBlockMeta::Deserialize(const char*& ptr)
    {
        block_id = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        dir_id = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        block_file_offset = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        payload_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        block_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        hash_value = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);

        /* file_ids */
        const uint64_t file_ids_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_ids.resize(file_ids_size);
        if (file_ids_size > 0)
        {
            std::memcpy(file_ids.data(), ptr, file_ids_size * sizeof(entry_id_t));
            ptr += file_ids_size * sizeof(entry_id_t);
        }

        /* file_orig_sizes */
        uint64_t original_file_sizes = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_orig_sizes.resize(original_file_sizes);
        if (original_file_sizes > 0)
        {
            std::memcpy(file_orig_sizes.data(), ptr, original_file_sizes * sizeof(uint64_t));
            ptr += original_file_sizes * sizeof(uint64_t);
        }

        /* file_compressed_sizes */
        uint64_t compressed_file_sizes = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_compressed_sizes.resize(compressed_file_sizes);
        if (compressed_file_sizes > 0)
        {
            std::memcpy(file_compressed_sizes.data(), ptr, compressed_file_sizes * sizeof(uint64_t));
            ptr += compressed_file_sizes * sizeof(uint64_t);
        }

        /* file_compressed_types */
        uint64_t compressed_file_types = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_compressed_types.resize(compressed_file_types);
        for (CompressionType& type : file_compressed_types)
        {
            type = static_cast<CompressionType>(DecodeFixed8(ptr));
            ptr += sizeof(uint8_t);
        }

        /* file_offsets */
        uint64_t file_offsets_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_offsets.resize(file_offsets_size);
        if (file_offsets_size > 0)
        {
            std::memcpy(file_offsets.data(), ptr, file_offsets_size * sizeof(uint64_t));
            ptr += file_offsets_size * sizeof(uint64_t);
        }

        /* file_hashes */
        const uint64_t file_xxhash_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_hashes.resize(file_xxhash_size);
        if (file_xxhash_size > 0)
        {
            std::memcpy(file_hashes.data(), ptr, file_xxhash_size * sizeof(uint64_t));
            ptr += file_xxhash_size * sizeof(uint64_t);
        }

        return {};
    }

    std::string DataBlockMeta::DebugString() const
    {
        return std::format("data_block_meta:  block_id: {}, block_size: {}, data_beg_pos:{}, dir_id:{}, "
                           "file_id_sizes:{},file_xxhash.size(), serialize_size:{}",
                           block_id, block_size, block_file_offset, dir_id, file_ids.size(), file_hashes.size(),
                           SerializedSize());
    }

    PackFileMeta::PackFileMeta()
    {
        dir_name_data.reserve(kEntryNameBufferSize);
        file_name_data.reserve(kEntryNameBufferSize);
        dir_id_to_child_dir_ids.reserve(64);
        dir_id_to_child_file_ids.reserve(64);
        dir_id_to_child_file_sizes.reserve(64);
    }

    void PackFileMeta::Clear()
    {
        dir_name_data.clear();
        file_name_data.clear();
        dir_entry_records.clear();
        file_entry_records.clear();
        dir_id_to_child_dir_ids.clear();
        dir_id_to_child_file_ids.clear();
        dir_id_to_child_file_sizes.clear();
    }

    void PackFileMeta::Serialize(std::string* dst) const
    {
        PutFixed64(dst, total_file_count);
        PutFixed64(dst, total_original_size);
        PutFixed64(dst, total_compressed_size);
        PutFixed64(dst, padding_size);

        PutFixed64(dst, dir_name_data.size());
        dst->append(dir_name_data);
        PutFixed64(dst, file_name_data.size());
        dst->append(file_name_data);

        dir_entry_records.serialize(dst);
        file_entry_records.serialize(dst);

        dir_id_to_child_dir_ids.serialize(dst);
        dir_id_to_child_file_ids.serialize(dst);
        dir_id_to_child_file_sizes.serialize(dst);
    }

    void PackFileMeta::Deserialize(const char*& ptr)
    {
        total_file_count = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        total_original_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        total_compressed_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        padding_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);

        const uint64_t dir_name_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        dir_name_data.assign(ptr, dir_name_size);
        skip_ptr(ptr, dir_name_size);

        const uint64_t file_name_size = DecodeFixed64(ptr);
        skip_ptr(ptr, FIX64_LEN);
        file_name_data.assign(ptr, file_name_size);
        skip_ptr(ptr, file_name_size);

        dir_entry_records.deserialize(ptr);
        file_entry_records.deserialize(ptr);

        dir_id_to_child_dir_ids.deserialize(ptr);
        dir_id_to_child_file_ids.deserialize(ptr);
        dir_id_to_child_file_sizes.deserialize(ptr);
    }

    size_t PackFileMeta::SerializedSize() const
    {
        size_t total_size =
            FixedFieldTotalSize(total_file_count, total_original_size, total_compressed_size, padding_size);
        total_size += ContainerSerializedTotalSize(dir_name_data, file_name_data);
        total_size += dir_entry_records.serialized_size();
        total_size += file_entry_records.serialized_size();
        total_size += dir_id_to_child_dir_ids.serialized_size();
        total_size += dir_id_to_child_file_ids.serialized_size();
        total_size += dir_id_to_child_file_sizes.serialized_size();
        return total_size;
    }

    void PackFileHandle::Clear()
    {
        dir_path_to_dir_id_.clear();
        dir_id_ = kRootDirId;
        file_id_ = kFileBeginId;
        pack_file_meta_.Clear();
    }

    void PackFileHandle::AddEntry(const entry_id_t key, std::string_view name, std::string* data_buf,
                                  SortedMap<entry_id_t, EntryRecord>& map, const EntryMeta& file_meta)
    {
        const uint64_t off = static_cast<uint64_t>(data_buf->size());
        const uint64_t len = static_cast<uint64_t>(name.size());
        const Handle handle{off, len};
        data_buf->append(name.data(), name.size());
        map.put(key, {handle, file_meta});
    }

    void PackFileHandle::AddDir(const entry_id_t dir_id, std::string_view dir_path, entry_id_t parent_dir_id,
                                const EntryMeta& file_meta)
    {
        AddEntry(dir_id, dir_path, &pack_file_meta_.dir_name_data, pack_file_meta_.dir_entry_records, file_meta);
        dir_path_to_dir_id_[std::string(dir_path)] = dir_id;
        if (parent_dir_id != 0)
        {
            pack_file_meta_.dir_id_to_child_dir_ids[parent_dir_id].push_back(dir_id);
        }
    }

    ResultStatus<std::string_view> PackFileHandle::GetDirPathByDirId(const entry_id_t dir_id) const
    {
        auto it = pack_file_meta_.dir_entry_records.find(dir_id);
        if (it != pack_file_meta_.dir_entry_records.end())
        {
            const auto& h = it->second.name_handle;
            return std::string_view(pack_file_meta_.dir_name_data.data() + h.offset, h.len);
        }
        return error_io("fail to get dir name of {}", dir_id);
    }

    [[nodiscard]] ResultStatus<entry_id_t> PackFileHandle::GetDirIdByDirPath(const std::string& dir_path) const
    {
        if (const auto it = dir_path_to_dir_id_.find(dir_path); it != dir_path_to_dir_id_.end())
        {
            return it->second;
        }
        return error_not_found("fail to get the dir id of {}", dir_path);
    }

    void PackFileHandle::AddFile(const entry_id_t key, std::string_view file_name, entry_id_t parent_dir_id,
                                 const EntryMeta& file_meta)
    {
        AddEntry(key, file_name, &pack_file_meta_.file_name_data, pack_file_meta_.file_entry_records, file_meta);
        if (parent_dir_id != 0)
        {
            pack_file_meta_.dir_id_to_child_file_ids[parent_dir_id].push_back(key);
            pack_file_meta_.dir_id_to_child_file_sizes[parent_dir_id].push_back(file_meta.file_orig_size);
        }
    }

    ResultStatus<std::string_view> PackFileHandle::GetFileNameByFileId(const entry_id_t key) const
    {
        if (auto it = pack_file_meta_.file_entry_records.find(key); it != pack_file_meta_.file_entry_records.end())
        {
            const auto& h = it->second.name_handle;
            return std::string_view(pack_file_meta_.file_name_data.data() + h.offset, h.len);
        }
        else
        {
            return error_not_found("fail to find the file name of {}", key);
        }
    }

    ResultStatus<ListColWidths> PackFileHandle::GetListWidthByFileId(const entry_id_t key) const
    {
        if (const auto it = pack_file_meta_.file_entry_records.find(key);
            it != pack_file_meta_.file_entry_records.end())
        {
            const auto& meta = it->second.entry_meta;
            return ListColWidths{SafeStrLen(meta.owner_name, sizeof(meta.owner_name)),
                                 SafeStrLen(meta.group_name, sizeof(meta.group_name)),
                                 std::formatted_size("{}", meta.file_orig_size)};
        }
        else
        {
            return error_not_found("fail to find entry by file_id {}", key);
        }
    }


    ResultStatus<entry_id_t> PackFileHandle::GetParentDirIdByPath(std::string_view entry_path)
    {
        std::filesystem::path path(entry_path);
        const auto parent_path = path.parent_path().string();
        auto it = dir_path_to_dir_id_.find(parent_path);
        if (it != dir_path_to_dir_id_.end())
        {
            return it->second;
        }
        return error_not_found("not found file id of dir: {}", entry_path);
    }

    const std::vector<entry_id_t>& PackFileHandle::GetChildDirIdsByDirId(entry_id_t parent_dir_id) const
    {
        const auto it = pack_file_meta_.dir_id_to_child_dir_ids.find(parent_dir_id);
        return (it != pack_file_meta_.dir_id_to_child_dir_ids.end()) ? it->second : empty_vec;
    }

    const std::vector<entry_id_t>& PackFileHandle::GetChildFileIdsByDirId(entry_id_t parent_dir_id) const
    {
        const auto it = pack_file_meta_.dir_id_to_child_file_ids.find(parent_dir_id);
        return (it != pack_file_meta_.dir_id_to_child_file_ids.end()) ? it->second : empty_vec;
    }

    const std::vector<uint64_t>& PackFileHandle::GetFileSizeByDirId(entry_id_t parent_dir_id) const
    {
        const auto it = pack_file_meta_.dir_id_to_child_file_sizes.find(parent_dir_id);
        return (it != pack_file_meta_.dir_id_to_child_file_sizes.end()) ? it->second : empty_vec;
    }

    void PackFileHandle::SetFileEntryMeta(const entry_id_t file_id, uint64_t block_id, const uint64_t offset,
                                          const uint64_t compressed_size)
    {
        pack_file_meta_.file_entry_records[file_id].entry_meta.block_id = block_id;
        pack_file_meta_.file_entry_records[file_id].entry_meta.offset = offset;
        pack_file_meta_.file_entry_records[file_id].entry_meta.file_compressed_size = compressed_size;
    }

    void PackFileHandle::SerializePackFileMeta(std::string* dst) const { pack_file_meta_.Serialize(dst); }

    Status PackFileHandle::DeserializePackFileMeta(const char*& ptr)
    {
        pack_file_meta_.Deserialize(ptr);
        for (const auto& key : pack_file_meta_.dir_entry_records | std::views::keys)
        {
            if (auto dir_name_opt = GetDirPathByDirId(key); dir_name_opt)
            {
                if (dir_name_opt)
                {
                    dir_path_to_dir_id_[std::string(dir_name_opt.value())] = key;
                }
            }
            else
            {
                return error_not_found("not found dir name of dir_id: {}", key);
            }
        }
        return {};
    }

    [[nodiscard]] size_t PackFileHandle::PackFileMetaSerializedSize() const { return pack_file_meta_.SerializedSize(); }

    std::expected<EntryMeta, ErrorStatus> PackFileHandle::ProcessDirEntry(const fs::directory_entry& entry)
    {
        EntryMeta file_meta{};
        file_meta.entry_type = entry.is_regular_file() ? EntryType::REGULAR : EntryType::DIRECTORY;
        file_meta.file_orig_size = entry.is_regular_file() ? fs::file_size(entry.path()) : 0;
        pack_file_meta_.total_original_size += file_meta.file_orig_size;
        if (entry.is_regular_file())
        {
            pack_file_meta_.total_file_count += 1;
        }
        const auto ftime = fs::last_write_time(entry.path());
        const auto sys_tp = std::chrono::file_clock::to_sys(ftime);
        file_meta.modify_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(sys_tp.time_since_epoch()).count();
        struct stat file_stat{};
        if (::stat(entry.path().c_str(), &file_stat) != 0)
        {
            return error_io("fail to get stat of {}: {}", entry.path().c_str(), strerror(errno));
        }
        file_meta.permissions = static_cast<uint32_t>(file_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
        if (passwd* pw = getpwuid(file_stat.st_uid); pw != nullptr)
        {
            std::snprintf(file_meta.owner_name, std::size(file_meta.owner_name), "%s", pw->pw_name);
        }
        else
        {
            std::snprintf(file_meta.owner_name, std::size(file_meta.owner_name), "%s", "unknown");
            std::println(std::cout, "failed to get uid of file {}, will use unknown instead", entry.path().c_str());
        }
        if (const group* gr = getgrgid(file_stat.st_gid); gr != nullptr)
        {
            std::snprintf(file_meta.group_name, std::size(file_meta.group_name), "%s", gr->gr_name);
        }
        else
        {
            std::snprintf(file_meta.group_name, std::size(file_meta.group_name), "%s", "unknown");
            std::println(std::cout, "failed to get group_name of file {}, will use unknown instead",
                         entry.path().c_str());
        }
        file_meta.block_id = 0;
        file_meta.offset = 0;
        return file_meta;
    }

    Status PackFileHandle::ScanDirectory(const std::string& root_path, bool recursive, bool filter_junk)
    {
        fs::path root(root_path);
        if (!fs::exists(root))
        {
            return error_io("directory '{}' is not exists", root_path);
        }
        AddDir(dir_id_, root_path);
        dir_id_++;
        std::vector<fs::directory_entry> entries;
        for (const auto& e : fs::directory_iterator(root))
        {
            // skip junk file and symlink
            if ((filter_junk && is_skip_entry(e)) || fs::is_symlink(e))
            {
                continue;
            }
            entries.push_back(e);
        }
        std::ranges::sort(entries,
                          [](const fs::directory_entry& a, const fs::directory_entry& b)
                          {
                              const bool a_dir = a.is_directory();
                              if (const bool b_dir = b.is_directory(); a_dir != b_dir)
                                  return a_dir;
                              return a.path().filename() < b.path().filename();
                          });
        for (const auto& e : entries)
        {
            auto result = ProcessDirEntry(e);
            std::string entry_path = e.path().string();
            if (!result)
            {
                return std::unexpected(result.error());
            }
            const EntryMeta& meta = result.value();
            if (auto get_status = GetParentDirIdByPath(entry_path))
            {
                entry_id_t parent_dir_id = get_status.value();
                if (meta.entry_type == EntryType::DIRECTORY)
                {
                    AddDir(dir_id_, entry_path, parent_dir_id, meta);
                    if (recursive)
                    {
                        if (auto scan_status = ScanDirectory(entry_path, true, filter_junk); !scan_status)
                        {
                            return scan_status;
                        }
                    }
                }
                else
                {
                    const std::string file_name = e.path().filename().string();
                    AddFile(file_id_, file_name, parent_dir_id, meta);
                    file_id_++;
                }
            }
            else
            {
                return std::unexpected(get_status.error());
            }
        }
        return {};
    }


    Status PackFileHandle::RenderDirListing(std::ostream& out, entry_id_t dir_id, const std::string& prefix,
                                            const OutputMode output_mode, bool human_readable, bool has_header) const
    {
        auto dir_name_res = GetDirPathByDirId(dir_id);
        if (!dir_name_res)
        {
            return error_io("fail to get dir name, dir_id: {}", dir_id);
        }
        const std::string_view dir_name = dir_name_res.value();
        if (has_header)
        {
            if (output_mode == OutputMode::TREE)
            {
                out << blue_start << dir_name << color_reset << '\n';
            }
            else if (output_mode == OutputMode::TSV)
            {
                out << kTsvMetaHeader;
            }
            has_header = false;
        }
        const auto& sub_dirs = GetChildDirIdsByDirId(dir_id);
        const auto& sub_files = GetChildFileIdsByDirId(dir_id);
        struct TreeEntry
        {
            std::string name;
            bool is_directory;
            entry_id_t id;
        };
        std::vector<TreeEntry> entries;
        entries.reserve(sub_dirs.size() + sub_files.size());
        for (entry_id_t sub_dir_id : sub_dirs)
        {
            auto dir_name_r = GetDirPathByDirId(sub_dir_id);
            if (!dir_name_r)
            {
                return error_io("fail to get sub‑dir name, dir_id: {}", sub_dir_id);
            }
            std::filesystem::path path_obj(dir_name_r.value());
            entries.emplace_back(path_obj.filename().string(), true, sub_dir_id);
        }
        for (entry_id_t sub_file_id : sub_files)
        {
            auto name_res = GetFileNameByFileId(sub_file_id);
            if (!name_res)
            {
                return error_io("fail to get file name, file_id: {}", sub_file_id);
            }
            entries.emplace_back(std::string(name_res.value()), false, sub_file_id);
        }
        std::ranges::sort(entries, [](const TreeEntry& a, const TreeEntry& b) { return a.name < b.name; });
        if (output_mode == OutputMode::TREE)
        {
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const auto& entry = entries[i];
                const bool is_last = (i == entries.size() - 1);
                out << prefix << (is_last ? "└── " : "├── ");
                if (entry.is_directory)
                {
                    out << blue_start;
                }
                out << entry.name << color_reset << '\n';
                if (entry.is_directory)
                {
                    std::string new_prefix = prefix + (is_last ? "    " : "│   ");
                    auto status =
                        RenderDirListing(out, entry.id, new_prefix, OutputMode::TREE, human_readable, has_header);
                    if (!status)
                        return status;
                }
            }
        }
        else
        {
            ListColWidths col_widths{0, 0, 0};
            if (output_mode == OutputMode::LIST)
            {
                for (entry_id_t sub_file_id : sub_files)
                {
                    auto res = GetListWidthByFileId(sub_file_id);
                    if (res)
                    {
                        col_widths.owner_name_width =
                            std::max(col_widths.owner_name_width, res.value().owner_name_width);
                        col_widths.group_name_width =
                            std::max(col_widths.group_name_width, res.value().group_name_width);
                        col_widths.file_size_width = std::max(col_widths.file_size_width, res.value().file_size_width);
                    }
                    else
                    {
                        return std::unexpected(res.error());
                    }
                }
            }
            for (const auto& entry : entries)
            {
                if (!entry.is_directory)
                {
                    auto it = pack_file_meta_.file_entry_records.find(entry.id);
                    if (it != pack_file_meta_.file_entry_records.end())
                    {
                        const auto& file_meta = it->second.entry_meta;
                        DirFileRef dir_file_ref = {dir_id, dir_name, entry.id, entry.name};
                        // std::string file_path = std::format("{}/{}", dir_name, entry.name);
                        file_meta.FormatOutput(out, dir_file_ref, output_mode, human_readable, col_widths);
                    }
                }
                else
                {
                    auto status = RenderDirListing(out, entry.id, prefix, output_mode, human_readable, has_header);
                    if (!status)
                        return status;
                }
            }
        }

        return {};
    }

} // namespace ctar
