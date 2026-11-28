#ifndef CTAR_READER_H
#define CTAR_READER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctar/error.h>

namespace ctar
{

    enum class EntryKind : uint8_t
    {
        kFile = 0,
        kDirectory,
    };

    /// One file or directory recorded in an archive.
    ///
    /// `path` is a virtual absolute path rooted at "/": the directory that was
    /// passed to `ctar pack` becomes "/", so the archive can be read without
    /// knowing where the source tree lived.
    struct Entry
    {
        std::string path;
        EntryKind kind = EntryKind::kFile;
        uint64_t size = 0; // original file size; 0 for directories
        int64_t mtime_ms = 0;
        uint32_t permissions = 0;
        std::string owner;
        std::string group;
    };

    /// Immutable, shared-ownership byte buffer.
    ///
    /// The data pointer stays valid while the owning `std::shared_ptr` is alive,
    /// so it can be handed to libraries that need a contiguous buffer (JPEG
    /// decoders, pyarrow, ...) without copying.
    class Buffer
    {
    public:
        Buffer() = default;

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        [[nodiscard]] const char* data() const noexcept { return data_.get(); }
        [[nodiscard]] size_t size() const noexcept { return size_; }
        [[nodiscard]] std::string_view view() const noexcept
        {
            return size_ == 0 ? std::string_view{} : std::string_view(data_.get(), size_);
        }

    private:
        friend class ArchiveReader;
        /// Created only by ArchiveReader::MakeBuffer.
        Buffer(std::unique_ptr<char[]> data, size_t size) noexcept;

        std::unique_ptr<char[]> data_;
        size_t size_ = 0;
    };

    /// A file opened from an archive.
    ///
    /// The archive format compresses each file as a whole (LZ4 has no seek
    /// points), so opening a file decompresses it into memory once; subsequent
    /// reads are served from that buffer. A `File` is not thread-safe; distinct
    /// `File` objects are independent. The reader that created it may be
    /// destroyed first.
    class File
    {
    public:
        File() = default;

        [[nodiscard]] uint64_t Size() const noexcept;
        /// Copies up to `dst.size()` bytes from the current position and
        /// advances it. Returns the number of bytes copied.
        [[nodiscard]] Result<size_t> Read(std::span<char> dst);
        /// Copies up to `dst.size()` bytes starting at `offset`. Returns the
        /// number of bytes copied; an offset past the end is an error.
        [[nodiscard]] Result<size_t> ReadAt(uint64_t offset, std::span<char> dst) const;
        /// The whole file. Shared ownership, no copy.
        [[nodiscard]] std::shared_ptr<const Buffer> ReadAll() const;

    private:
        friend class ArchiveReader;
        explicit File(std::shared_ptr<const Buffer> data) noexcept;

        std::shared_ptr<const Buffer> data_;
        size_t offset_ = 0;
    };

    struct ReaderOptions
    {
        /// Reject archives whose uncompressed metadata exceeds this many bytes.
        /// 0 means unlimited. Matches the CLI's --max-metadata-size.
        uint64_t max_metadata_size = 1ULL << 30;
        /// Reject files whose original size, or whose stored size in the
        /// archive, exceeds this many bytes. 0 means unlimited. Matches the
        /// CLI's --max-decompressed-size.
        uint64_t max_file_size = 1ULL << 30;
    };

    /// Read-only, random-access view of a `.ctar` archive.
    ///
    /// Open loads and validates the metadata index; List/Stat only consult it.
    /// Reading one file touches only that file's stored bytes, never extracts
    /// the archive to disk. After Open the reader is immutable, and read calls
    /// may be made concurrently from several threads (the underlying I/O uses
    /// pread); individual `File` objects are not thread-safe.
    class ArchiveReader
    {
    public:
        ArchiveReader() noexcept;
        ~ArchiveReader();

        ArchiveReader(ArchiveReader&&) noexcept;
        ArchiveReader& operator=(ArchiveReader&&) noexcept;
        ArchiveReader(const ArchiveReader&) = delete;
        ArchiveReader& operator=(const ArchiveReader&) = delete;

        /// Opens an archive and loads its metadata index.
        static Result<ArchiveReader> Open(std::string archive_path, ReaderOptions options = {});

        /// Lists the direct children of `dir` (or every descendant when
        /// `recursive` is true), sorted by path in byte order.
        [[nodiscard]] Result<std::vector<Entry>> List(std::string_view dir = "/", bool recursive = false) const;

        /// Metadata for one file or directory.
        [[nodiscard]] Result<Entry> Stat(std::string_view path) const;

        /// Opens a file and decompresses it into memory (bounded by
        /// ReaderOptions::max_file_size).
        [[nodiscard]] Result<File> OpenFile(std::string_view path) const;

        /// Convenience wrapper around OpenFile().ReadAll().
        [[nodiscard]] Result<std::shared_ptr<const Buffer>> ReadFile(std::string_view path) const;

    private:
        struct Impl;
        explicit ArchiveReader(std::unique_ptr<Impl> impl) noexcept;
        static std::shared_ptr<const Buffer> MakeBuffer(std::unique_ptr<char[]> data, size_t size);

        std::unique_ptr<Impl> impl_;
    };

} // namespace ctar

#endif // CTAR_READER_H
