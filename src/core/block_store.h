#ifndef CTAR_BLOCK_STORE_H
#define CTAR_BLOCK_STORE_H
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "file_format.h"
#include "options.h"
#include "src/thread_pool/thread_pool.h"
#include "src/utils/status.h"
namespace ctar
{
    namespace fs = std::filesystem;
    using entry_id_t = uint64_t;

    enum class TaskOperation
    {
        Pack,
        Extract
    };


    ResultStatus<uint64_t> Hash(const std::string& file_path);
    ResultStatus<uint64_t> Hash(const void* data, size_t len);

    class BlockStore
    {
    private:
        PackFileHeader pack_file_header_;

        PackFileHandle pack_file_handle_;

        std::vector<DataBlockMeta> data_block_metas_;

        // Size of the metadata region at the front of the pack file, made up of
        // four parts:
        //   1. pack file header     (PackFileHeader)
        //   2. file-handle metadata (PackFileHandle)
        //   3. block count
        //   4. DataBlockMeta per block
        // Always 4K-aligned. Because the region starts at file offset 0, this value
        // is also the offset where the block region begins.
        uint64_t meta_region_size_ = 0;

        // Size of the block region: the sum of the 4K-aligned size of every block.
        // Filled in by the writer thread; the pack file is finally truncated to
        // meta_region_size_ + block_region_size_.
        uint64_t block_region_size_ = 0;

        // Upper bound on block_region_size_, accumulated in BuildBlocksFromFileStats
        // from the *uncompressed* block sizes. Used to extend the file up front so
        // the writer never writes past EOF. A true bound: compression never expands,
        // since a block that would grow falls back to kNone (output size == input).
        uint64_t max_block_region_size_ = 0;

        // Total compressed size of all files
        uint64_t total_compressed_size_ = 0;

        uint64_t last_write_block_id_ = 0;

        struct BlockBuffer
        {
            std::string data;
        };

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
        CompressionConfig compression_config_;
        unsigned thread_cnt_ = 1;

        [[nodiscard]] uint64_t GetMetaSize() const;

        /// @brief Packs one block in place (hash + compress + queue to writer); blocks on the pool.
        /// @param block_meta metadata of block
        Status PackTask(DataBlockMeta& block_meta);

        Status UnPackTask(const DataBlockMeta& block_meta);

        void WriterLoop();

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
        /// @return Status OK when build success
        Status RestoreBlocksFromPack(const std::string& pack_file_path);


        /// Submit pack or unpack tasks to the thread pool.
        /// For pack operations, a dedicated writer thread will persist packed data to the output file.
        /// Blocks until all tasks complete, performs error checking, and removes the output file on failure.
        /// @param op Task operation type
        /// @return Status OK when all tasks succeed; error otherwise
        Status SubmitAndAwaitTasks(TaskOperation op);

        /// Run the full packing pipeline: partition blocks, submit pack tasks,
        /// block until all tasks finish, and write the final pack archive.
        /// @param output_file Path of the generated pack file.
        Status PackPipeline(const std::string& output_file);

        /// Run the full unpacking pipeline: read pack file, restore blocks,
        /// submit unpack tasks, and block until all extraction completes.
        /// @param input_file Path to the source pack archive.
        /// @param output_dir Directory to restore extracted files.
        Status UnPackPipeline(const std::string& input_file, const std::string& output_dir);

        [[nodiscard]] Status CreateUnpackDir() const;


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
    };
}; // namespace ctar
#endif // CTAR_BLOCK_STORE_H
