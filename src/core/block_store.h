#ifndef CTAR_BLOCK_STORE_H
#define CTAR_BLOCK_STORE_H
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "file_format.h"
#include "options.h"
#include "src/thread_pool/thread_pool.h"
#include "src/utils/status.h"
namespace ctar
{
    namespace fs = std::filesystem;
    using entry_id_t = uint64_t;

    class WriteFile;

    enum class TaskOperation
    {
        Pack,
        Extract
    };

    /// @brief Where the serialized metadata is stored and what the header records.
    struct MetadataStoragePlan
    {
        bool use_compressed = false;
        uint64_t stored_size = 0; // bytes written to the archive
        uint64_t header_size = 0; // metadata_compressed_size
        uint64_t header_type = 0; // metadata_compressed_type
    };

    /// @brief Choose between the compressed and raw metadata bytes.
    ///
    /// Compression is used only when it succeeded and produced a smaller buffer.
    /// Otherwise the raw bytes are stored and the header records kNone, so the
    /// fallback path is explicit and testable.
    /// @param raw_size uncompressed metadata size
    /// @param compressed_size result from Compress(); 0 means failure
    /// @param compressed_type compression type that was attempted
    [[nodiscard]] MetadataStoragePlan PlanMetadataStorage(uint64_t raw_size, uint64_t compressed_size,
                                                          uint64_t compressed_type);


    /// @brief XXH64 of a file's contents, using the archive hash seed.
    ResultStatus<uint64_t> Hash(const std::string& file_path);

    /// @brief Resolve a directory path recorded in an archive under @p root.
    /// The archive is untrusted: a leading root is stripped, and an entry that would
    /// end up outside @p root is rejected instead of being written there.
    /// @param root output directory the entry has to stay inside
    /// @param stored_dir directory path exactly as recorded in the archive
    /// @return resolved path, or error_corruption when the entry escapes @p root
    ResultStatus<fs::path> ResolveArchiveDirectory(const fs::path& root, const fs::path& stored_dir);

    /// @brief Resolve a file entry recorded in an archive under @p root.
    /// @param root output directory the entry has to stay inside
    /// @param stored_dir directory path exactly as recorded in the archive
    /// @param file_name file name exactly as recorded in the archive, must be a single
    ///        path component (no separators, not "." or "..")
    /// @return resolved path, or error_corruption when the entry escapes @p root
    ResultStatus<fs::path> ResolveArchiveFile(const fs::path& root, const fs::path& stored_dir,
                                              std::string_view file_name);

    class BlockStore
    {
    private:
        PackFileHeader pack_file_header_;

        PackFileHandle pack_file_handle_;

        std::vector<DataBlockMeta> data_block_metas_;

        // 1.0.0 layout only: size of the front metadata region (header + metadata).
        // In 1.1.0 the metadata lives at the end and this stays 0.
        uint64_t meta_region_size_ = 0;

        // Start of the file-data (block) region: 4096 in 1.1.0, meta_region_size_ in
        // the 1.0.0 layout.
        uint64_t file_data_offset_ = 0;

        // Size of the file-data region: the sum of every block payload (plus block
        // padding in the 1.0.0 layout). Filled in by the writer thread.
        uint64_t block_region_size_ = 0;

        // Size of the stored metadata blob (compressed in 1.1.0 when above threshold).
        uint64_t metadata_original_size_ = 0;
        uint64_t metadata_compressed_size_ = 0;
        uint64_t metadata_compressed_type_ = 0;
        uint64_t metadata_checksum_ = 0;

        // 1.0.0 layout only: size of the metadata region after the header.
        uint64_t legacy_file_meta_size_ = 0;

        uint64_t archive_size_ = 0;
        bool legacy_layout_ = false;

        // Upper bound on block_region_size_, accumulated in BuildBlocksFromFileStats
        // from the *uncompressed* block sizes. Used to extend the file up front so
        // the writer never writes past EOF. A true bound: compression never expands,
        // since a block that would grow falls back to kNone (output size == input).
        uint64_t max_block_region_size_ = 0;

        // Total compressed size of all files
        uint64_t file_data_compressed_size_ = 0;

        uint64_t last_write_block_id_ = 0;

        /// A byte buffer whose contents are only ever written through Data().
        ///
        /// A raw allocation is used on purpose: `new char[n]` leaves the bytes
        /// uninitialised (no zeroing cost), and raw memory is outside the container
        /// annotations that make ASan report writes past a std::string's size.
        struct RawBuffer
        {
            std::unique_ptr<char[]> data;
            size_t capacity = 0;

            void EnsureCapacity(size_t bytes)
            {
                if (capacity < bytes)
                {
                    // `new char[n]` is default-initialisation: the bytes stay
                    // uninitialised. `new char[n]()` or `std::make_unique<char[]>(n)`
                    // would value-initialise (zero) the buffer -- do not switch to
                    // those unless the zeroing is actually wanted.
                    data.reset(new char[bytes]);
                    capacity = bytes;
                }
            }

            [[nodiscard]] char* Data() noexcept { return data.get(); }
            [[nodiscard]] const char* Data() const noexcept { return data.get(); }
            [[nodiscard]] size_t Capacity() const noexcept { return capacity; }
            void Release() noexcept
            {
                data.reset();
                capacity = 0;
            }

            /// Cap the retained capacity after a task. A buffer that grew beyond
            /// @p keep is freed and replaced with a small reusable one; the contents
            /// are dead once the task is done, so nothing is copied. noexcept because
            /// this runs from a scope guard; a failed allocation leaves capacity at 0.
            void TrimTo(const size_t keep) noexcept
            {
                if (capacity <= keep)
                {
                    return;
                }
                data.reset();
                capacity = 0;
                if (keep == 0)
                {
                    return;
                }
                data.reset(new (std::nothrow) char[keep]);
                if (data)
                {
                    capacity = keep;
                }
            }
        };

        /// One compressed block payload, recycled through the buffer pool.
        using BlockBuffer = RawBuffer;

        struct PendingWriteItem
        {
            uint64_t block_id = 0;
            std::unique_ptr<BlockBuffer> buffer;
        };
        std::mutex pack_queue_mutex_;
        std::condition_variable block_buffer_available_;
        std::condition_variable write_ready_;
        std::deque<std::unique_ptr<BlockBuffer>> available_block_buffers_;
        std::deque<PendingWriteItem> pending_write_items_;
        bool pack_write_done_ = false;
        std::optional<ErrorStatus> writer_error_;
        std::string packed_file_;
        std::string target_path_;

        // Directories created by the current unpack run, deepest last. Used to
        // restore an empty destination when extraction fails; only maintained when the
        // destination already existed (otherwise the whole directory is removed).
        std::vector<fs::path> created_dirs_;
        bool track_created_dirs_ = false;
        CompressionConfig compression_config_;
        unsigned thread_cnt_ = 1;
        // Per-file decompression budget for unpack; 0 means unlimited.
        uint64_t max_decompressed_file_size_ = kDefaultMaxDecompressedFileSize;
        // Per-archive uncompressed metadata budget for every reader; 0 means unlimited.
        uint64_t max_metadata_size_ = kDefaultMaxMetadataSize;

        [[nodiscard]] uint64_t GetRawMetaSize() const;

        /// @brief Packs one block in place (hash + compress + queue to writer); blocks on the pool.
        /// @param block_meta metadata of block
        Status PackTask(DataBlockMeta& block_meta);

        Status UnPackTask(const DataBlockMeta& block_meta);

        /// @param write_file exclusively opened output; owned by the caller and
        ///        outlives the writer thread. The writer must not reopen by path.
        void WriterLoop(WriteFile* write_file);

        /// @brief acquire BlockBuffer from available_block_buffers_
        std::unique_ptr<BlockBuffer> AcquireBlockBuffer();

        /// @brief Return a single BlockBuffer back to available_block_buffers_ for reuse
        /// @param buffer BlockBuffer to return, ownership is transferred to the callee
        void ReturnBlockBuffer(std::unique_ptr<BlockBuffer> buffer);

        /// @brief Return a batch of BlockBuffers back to available_block_buffers_ for reuse
        /// @param buffers List of BlockBuffers to return, full container ownership is transferred
        void ReturnBlockBuffers(std::vector<std::unique_ptr<BlockBuffer>> buffers);

        bool EnqueueBlockBuffer(uint64_t block_id, std::unique_ptr<BlockBuffer> buffer);


    public:
        BlockStore()
        {
            const unsigned hw = std::thread::hardware_concurrency();
            thread_cnt_ = (hw == 0) ? 1 : static_cast<int>(hw);
        }

        explicit BlockStore(const unsigned thread_cnt, const CompressionConfig compression_info) :
            compression_config_(compression_info), thread_cnt_(thread_cnt)
        {
            if (thread_cnt_ < 1)
            {
                thread_cnt_ = 1;
            }
        }

        ~BlockStore() = default;

        /// Split file metadata into blocks according to file statistics.
        /// Prepares block layout before packing.
        /// @return Status OK when build success
        Status BuildBlocksFromFileStats();

        /// Restore block structures by reading from an existing pack file.
        /// Reconstructs block metadata from packed archive.
        /// @param pack_file_path file path of pack file
        /// @param max_decompressed_file_size per-file decompression budget; 0 means unlimited
        /// @return Status OK when build success
        Status RestoreBlocksFromPack(const std::string& pack_file_path,
                                     uint64_t max_decompressed_file_size = kDefaultMaxDecompressedFileSize);


        /// Submit pack or unpack tasks to the thread pool.
        /// For pack operations, a dedicated writer thread will persist packed data to the output file.
        /// Blocks until all tasks complete, performs error checking, and removes the output file on failure.
        /// @param op Task operation type
        /// @return Status OK when all tasks succeed; error otherwise
        Status SubmitAndAwaitTasks(TaskOperation op);

        /// Run the full packing pipeline: partition blocks, submit pack tasks,
        /// block until all tasks finish, and write the final pack archive.
        /// @param output_file Path of the generated pack file.
        /// @param force Replace the output file if it already exists.
        Status PackPipeline(const std::string& output_file, bool force = false);

        /// Run the full unpacking pipeline: read pack file, restore blocks,
        /// submit unpack tasks, and block until all extraction completes.
        /// @param input_file Path to the source pack archive.
        /// @param output_dir Directory to restore extracted files.
        /// @param force Allow a non-empty output directory and replace its files.
        Status UnPackPipeline(const std::string& input_file, const std::string& output_dir, bool force = false);

        /// @brief Set the per-file decompression budget used by unpack (0 = unlimited).
        void SetMaxDecompressedFileSize(const uint64_t bytes) { max_decompressed_file_size_ = bytes; }

        /// @brief Set the per-archive uncompressed metadata budget (0 = unlimited).
        void SetMaxMetadataSize(const uint64_t bytes) { max_metadata_size_ = bytes; }

        [[nodiscard]] Status CreateUnpackDir();


        /// @brief Render directory tree view and print to output stream
        /// @param target_dir Root directory to render, empty string uses current directory
        /// @param has_header Whether to print header line at the beginning
        [[nodiscard]] Status RenderDirectoryTree(const std::string& target_dir = "", bool has_header = true) const;

        /// @brief Render directory tree structure and write content to output stream
        /// @param out Output stream for writing tree formatted directory data
        Status RenderDirectoryTree(std::ostream& out) const;

        /// @brief Dump directory metadata tree to output stream
        /// @param out Target output stream
        /// @param target_dir Root directory to traverse; empty string for current directory
        /// @param human_readable If true, format file sizes into human-readable unit strings
        /// @param output_mode Output format: LIST / TREE / TSV
        /// @param has_title Whether to prepend header title line
        Status DumpDirectoryTree(std::ostream& out, const std::string& target_dir = "", bool human_readable = false,
                                 OutputMode output_mode = OutputMode::LIST, bool has_title = false) const;

        /// @brief Scan directory entries under root path
        /// @param root_path Root directory path to scan
        /// @param recursive If true, recursively traverse subdirectories
        /// @return Status Ok on success, error status if scan failed
        [[nodiscard]] Status Scan(const std::string& root_path, bool recursive = true);

        [[nodiscard]] FileStats GetFileStats() const { return pack_file_handle_.GetFileStats(); }

        /// @brief Actual size of the pack file written by the last successful PackPipeline.
        [[nodiscard]] uint64_t GetPackFileSize() const noexcept
        {
            return file_data_offset_ + block_region_size_ + metadata_compressed_size_ + sizeof(kMagicNumber);
        }

        /// @brief Serialized size of the archive header (4096 in 1.1.0, 16 in 1.0.0).
        [[nodiscard]] uint64_t GetHeaderSize() const noexcept
        {
            return legacy_layout_ ? LegacyPackFileHeader::kSerializedSize : PackFileHeader::kSerializedSize;
        }

        /// @brief Size of the stored metadata region.
        [[nodiscard]] uint64_t GetFileMetaSize() const noexcept
        {
            return legacy_layout_ ? legacy_file_meta_size_ : metadata_compressed_size_;
        }

        [[nodiscard]] uint64_t GetBlockCount() const { return data_block_metas_.size(); }

        [[nodiscard]] size_t GetDirectoryCount() const { return pack_file_handle_.GetDirectoryCount(); }

        [[nodiscard]] bool IsLegacyLayout() const noexcept { return legacy_layout_; }

        [[nodiscard]] const PackFileHeader& GetPackHeader() const noexcept { return pack_file_header_; }

        // Read-only access to the validated index. Internal: these are used by
        // the public ArchiveReader and are not part of the installed headers.
        [[nodiscard]] const PackFileHandle& GetPackFileHandle() const noexcept { return pack_file_handle_; }
        [[nodiscard]] const std::vector<DataBlockMeta>& GetDataBlockMetas() const noexcept
        {
            return data_block_metas_;
        }

        /// @brief Read and validate only the archive header; no metadata is parsed.
        [[nodiscard]] Status ReadPackHeaderOnly(const std::string& pack_file_path);
    };
}; // namespace ctar
#endif // CTAR_BLOCK_STORE_H
