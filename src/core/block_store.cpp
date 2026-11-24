#include <algorithm>
#include <chrono>
#include <expected>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <map>
#include <ranges>
#include <sys/uio.h>
#include <thread>
#include <unordered_set>
#include <unistd.h>
#include <xxhash.h>

#include "block_store.h"
#include "compress.h"
#include "file_handle.h"
#include "src/utils/serialize_helper.h"
#include "src/utils/coding.h"
#include "src/utils/logger.h"
#include "src/utils/status.h"

namespace ctar
{
#ifndef O_BINARY
#define O_BINARY 0
#endif

    namespace
    {
        uint64_t CurrentTimeMillis()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());
        }

        bool IsValidCompressionType(const uint64_t type)
        {
            return type == static_cast<uint64_t>(CompressionType::kNone) ||
                type == static_cast<uint64_t>(CompressionType::kLZ4) ||
                type == static_cast<uint64_t>(CompressionType::kLZ4HC);
        }
    } // namespace


    ResultStatus<uint64_t> Hash(const std::string& file_path)
    {
        auto read_file_status = NewPosixReadFile(file_path);
        if (!read_file_status)
        {
            return std::unexpected(read_file_status.error());
        }
        auto& read_file = read_file_status.value();
        auto size_status = read_file->GetFileSize();
        if (!size_status)
        {
            return std::unexpected(size_status.error());
        }
        const uint64_t file_size = size_status.value();
        std::vector<char> buf(file_size);
        auto read_status = read_file->Read(file_size, buf.data());
        if (!read_status || read_status.value() != file_size)
        {
            return error_io("failed to read data from {}", file_path);
        }
        return XXH64(buf.data(), buf.size(), kHashSeed);
    }

    MetadataStoragePlan PlanMetadataStorage(const uint64_t raw_size, const uint64_t compressed_size,
                                            const uint64_t compressed_type)
    {
        if (compressed_size > 0 && compressed_size < raw_size)
        {
            return {.use_compressed = true,
                    .stored_size = compressed_size,
                    .header_size = compressed_size,
                    .header_type = compressed_type};
        }
        return {.use_compressed = false,
                .stored_size = raw_size,
                .header_size = raw_size,
                .header_type = static_cast<uint64_t>(CompressionType::kNone)};
    }


    namespace
    {
        /// Join an archive-stored path under @p root and reject anything that would
        /// escape it. The archive is untrusted: its paths may be absolute or contain
        /// "..", and neither may reach outside the output directory.
        ResultStatus<fs::path> ResolveUnderRoot(const fs::path& root, const fs::path& stored)
        {
            // relative_path() drops a leading root, so an archive that recorded an
            // absolute path still lands inside the output directory.
            const fs::path relative = stored.relative_path().lexically_normal();
            if (relative.empty())
            {
                return error_corruption("archive entry '{}' has an empty path", stored.string());
            }
            for (const fs::path& part : relative)
            {
                if (part == "..")
                {
                    return error_corruption("archive entry '{}' escapes the output directory", stored.string());
                }
            }
            return (root / relative).lexically_normal();
        }
    } // namespace

    ResultStatus<fs::path> ResolveArchiveDirectory(const fs::path& root, const fs::path& stored_dir)
    {
        return ResolveUnderRoot(root, stored_dir);
    }

    ResultStatus<fs::path> ResolveArchiveFile(const fs::path& root, const fs::path& stored_dir,
                                              const std::string_view file_name)
    {
        const fs::path name(file_name);
        // A file entry names exactly one component: a crafted archive could otherwise
        // put separators or ".." here and place the file somewhere else.
        if (name.empty() || name.has_parent_path() || name == "." || name == "..")
        {
            return error_corruption("archive entry has an invalid file name '{}'", file_name);
        }
        auto dir = ResolveUnderRoot(root, stored_dir);
        if (!dir)
        {
            return std::unexpected(dir.error());
        }
        return dir.value() / name;
    }

    Status BlockStore::Scan(const std::string& root_path, bool recursive)
    {
        return pack_file_handle_.ScanDirectory(root_path, recursive);
    }

    Status BlockStore::RenderDirectoryTree(const std::string& target_dir, bool has_header) const
    {
        entry_id_t dir_file_id = kRootDirId;
        if (!target_dir.empty())
        {
            if (auto s = pack_file_handle_.GetDirIdByDirPath(target_dir))
            {
                dir_file_id = s.value();
            }
            else
            {
                return std::unexpected(s.error());
            }
        }
        auto s = pack_file_handle_.RenderDirListing(std::cout, dir_file_id, "", OutputMode::TREE, false, has_header);
        return s;
    }


    Status BlockStore::DumpDirectoryTree(std::ostream& out, const std::string& target_dir, bool human_readable,
                                         OutputMode output_mode, bool has_title) const
    {
        entry_id_t dir_file_id = kRootDirId;
        if (!target_dir.empty())
        {
            if (auto s = pack_file_handle_.GetDirIdByDirPath(target_dir))
            {
                dir_file_id = s.value();
            }
            else
            {
                return std::unexpected(s.error());
            }
        }
        auto s = pack_file_handle_.RenderDirListing(out, dir_file_id, "", output_mode, human_readable, has_title);
        return s;
    }


    Status BlockStore::RenderDirectoryTree(std::ostream& out) const
    {
        auto s = pack_file_handle_.RenderDirListing(out);
        return s;
    }

    std::unique_ptr<BlockStore::BlockBuffer> BlockStore::AcquireBlockBuffer()
    {
        std::unique_lock lock(pack_queue_mutex_);
        block_buffer_available_.wait(lock,
                                     [this] { return !available_block_buffers_.empty() || writer_error_.has_value(); });
        if (writer_error_)
        {
            return {};
        }
        auto buffer = std::move(available_block_buffers_.front());
        available_block_buffers_.pop_front();
        return buffer;
    }

    void BlockStore::ReturnBlockBuffer(std::unique_ptr<BlockBuffer> buffer)
    {
        if (!buffer)
        {
            return;
        }
        // Blocks larger than the normal ceiling are not worth keeping in the pool.
        if (buffer->Capacity() > kMaxFileSizePerBlock)
        {
            buffer->Release();
        }
        {
            std::lock_guard lock(pack_queue_mutex_);
            available_block_buffers_.push_back(std::move(buffer));
        }
        // Only one buffer was released, so at most one waiter can proceed.
        block_buffer_available_.notify_one();
    }

    void BlockStore::ReturnBlockBuffers(std::vector<std::unique_ptr<BlockBuffer>> buffers)
    {
        if (buffers.empty())
            return;
        {
            std::lock_guard lock(pack_queue_mutex_);
            for (auto& buffer : buffers)
            {
                if (!buffer)
                {
                    continue;
                }
                // Blocks larger than the normal ceiling are not worth keeping in the pool.
                if (buffer->Capacity() > kMaxFileSizePerBlock)
                {
                    buffer->Release();
                }
                available_block_buffers_.push_back(std::move(buffer));
            }
        }
        // Up to buffers.size() waiters can proceed
        block_buffer_available_.notify_all();
    }

    bool BlockStore::EnqueueBlockBuffer(const uint64_t block_id, std::unique_ptr<BlockBuffer> buffer)
    {
        std::lock_guard lock(pack_queue_mutex_);
        if (writer_error_ || pack_write_done_)
        {
            return false;
        }
        pending_write_items_.push_back({block_id, std::move(buffer)});
        write_ready_.notify_one();
        return true;
    }

    void BlockStore::WriterLoop()
    {
        auto w_res = NewPosixWriteFile(packed_file_);
        if (!w_res)
        {
            std::unique_lock lock(pack_queue_mutex_);
            writer_error_ = w_res.error();
            return;
        }
        auto& w_file = w_res.value();
        while (true)
        {
            std::vector<PendingWriteItem> all_write_tasks;
            {
                std::unique_lock lock(pack_queue_mutex_);
                write_ready_.wait(
                    lock,
                    [this] { return !pending_write_items_.empty() || pack_write_done_ || writer_error_.has_value(); });

                if (writer_error_.has_value() || (pending_write_items_.empty() && pack_write_done_))
                {
                    break;
                }
                all_write_tasks.reserve(pending_write_items_.size());
                while (!pending_write_items_.empty())
                {
                    all_write_tasks.push_back(std::move(pending_write_items_.front()));
                    pending_write_items_.pop_front();
                }
            }
            size_t pos = 0;
            const size_t total = all_write_tasks.size();
            while (pos < total)
            {
                std::vector<std::unique_ptr<BlockBuffer>> return_buffers;
                // Batch by entry count and payload bytes: many small blocks share one
                // pwritev, while one large block stays on its own.
                size_t batch_end = pos;
                size_t batch_bytes = 0;
                while (batch_end < total && batch_end - pos < kMaxBatchWriteEntries)
                {
                    const uint64_t block_len = data_block_metas_[all_write_tasks[batch_end].block_id].block_size;
                    if (batch_end > pos && batch_bytes + block_len > kMaxBatchWriteBytes)
                    {
                        break;
                    }
                    batch_bytes += block_len;
                    ++batch_end;
                }
                const size_t batch_cnt = batch_end - pos;
                std::vector<iovec> iovs;
                iovs.reserve(batch_cnt);
                uint64_t batch_start = block_region_size_;
                for (size_t i = pos; i < batch_end; ++i)
                {
                    auto& pending = all_write_tasks[i];
                    last_write_block_id_ = pending.block_id;
                    auto& task = data_block_metas_[pending.block_id];
                    task.block_file_offset = block_region_size_;
                    file_data_compressed_size_ += std::accumulate(task.file_compressed_sizes.begin(),
                                                              task.file_compressed_sizes.end(), size_t{0});
                    // A block whose files are all empty has no payload at all; writing a
                    // zero-length iovec for it would only waste a syscall.
                    if (task.block_size > 0)
                    {
                        iovs.push_back({.iov_base = pending.buffer->Data(), .iov_len = task.block_size});
                    }
                    block_region_size_ += task.block_size;
                }
                Status write_status;
                if (!iovs.empty())
                {
                    write_status = w_file->RandomWriteV(iovs.data(), static_cast<int>(iovs.size()),
                                                        file_data_offset_ + batch_start);
                }
                if (!write_status)
                {
                    batch_end = total;
                    std::lock_guard lock(pack_queue_mutex_);
                    writer_error_ =
                        error_io("failed to write block to file: {}", write_status.error().err_message).error();
                }
                for (size_t i = pos; i < batch_end; ++i)
                {
                    return_buffers.push_back(std::move(all_write_tasks[i].buffer));
                }
                ReturnBlockBuffers(std::move(return_buffers));
                if (!write_status)
                {
                    return;
                }
                pos = batch_end;
            }
        }
    }

    Status BlockStore::PackTask(DataBlockMeta& block_meta)
    {
        thread_local RawBuffer tls_read_buf;
        tls_read_buf.EnsureCapacity(block_meta.dir_stats.max_file_size);
        struct TrimReadBufferOnExit
        {
            RawBuffer& buffer;
            ~TrimReadBufferOnExit() { buffer.TrimTo(kMaxRetainedBufferBytes); }
        } trim_read_buffer{tls_read_buf};
        auto compress_buffer = AcquireBlockBuffer();
        if (!compress_buffer)
        {
            std::lock_guard lock(pack_queue_mutex_);
            if (writer_error_)
            {
                return std::unexpected(*writer_error_);
            }
            return error_run_time("pack buffer unavailable");
        }
        auto compress_buffer_size =
            compress::MaxCompressBufferSize(compression_config_.compression_type, block_meta.block_size);
        compress_buffer->EnsureCapacity(static_cast<size_t>(compress_buffer_size));

        uint64_t payload_offset = 0;
        size_t file_count = block_meta.file_ids.size();
        CompressionType compression_type;
        for (size_t i = 0; i < file_count; ++i)
        {
            entry_id_t fid = block_meta.file_ids[i];
            bool has_compressed = false;
            uint64_t compressed_size = 0;
            compression_type = compression_config_.compression_type;
            auto file_name_status = pack_file_handle_.GetFileNameByFileId(fid);
            if (!file_name_status)
            {
                ReturnBlockBuffer(std::move(compress_buffer));
                return error_not_found("can't find file name of file id {}", fid);
            }
            std::string file_path = fs::path(block_meta.dir_stats.dir_path) / std::string(file_name_status.value());
            const uint64_t original_size = block_meta.file_orig_sizes[i];
            if (original_size == 0)
            {
                // Nothing to read or compress, but the reader still verifies the hash of
                // every entry, so it has to be recorded here as well.
                block_meta.file_hashes[i] = kZeroXXHash;
                compression_type = CompressionType::kNone;
            }
            else
            {
                if (auto read_file_status = NewPosixReadFile(file_path))
                {
                    auto& rf = read_file_status.value();
                    auto read_status = rf->Read(original_size, tls_read_buf.Data());
                    if (!read_status || read_status.value() != original_size)
                    {
                        ReturnBlockBuffer(std::move(compress_buffer));
                        return error_io("fail to read data from {}", file_path);
                    }
                }
                else
                {
                    ReturnBlockBuffer(std::move(compress_buffer));
                    return error_not_found("read file fail: {}", file_path);
                }
            }
            bool skip_compression = ShouldSkipCompression(file_name_status.value());

            const bool can_compress = !skip_compression && original_size <= kMaxInputSize && original_size > 0;
            block_meta.file_hashes[i] = XXH64(tls_read_buf.Data(), original_size, kHashSeed);
            if (can_compress)
            {
                compressed_size = compress::Compress(compression_type, tls_read_buf.Data(),
                                                     compress_buffer->Data() + payload_offset, original_size,
                                                     compression_config_.compression_param);
                if (compressed_size > 0 && compressed_size < original_size)
                {
                    has_compressed = true;
                }
                else if (compressed_size == 0)
                {
                    ReturnBlockBuffer(std::move(compress_buffer));
                    return error_run_time("file id {} produced an empty payload", block_meta.file_ids[i]);
                }
            }
            // 1. skip_compression = true
            // 2. compressed_size >= original_size
            // 3. compress failed
            if (!has_compressed && original_size > 0)
            {
                compressed_size = compress::Compress(CompressionType::kNone, tls_read_buf.Data(),
                                                     compress_buffer->Data() + payload_offset, original_size);
                compression_type = CompressionType::kNone;
            }
            block_meta.file_offsets[i] = payload_offset;
            block_meta.file_compressed_sizes[i] = compressed_size;
            block_meta.file_compressed_types[i] = compression_type;
            payload_offset += compressed_size;
        }

        block_meta.payload_size = payload_offset;
        // Blocks are written back to back; only the start of the block region is
        // 4K-aligned. Padding every block to 4K wasted up to 4 KiB per block and made
        // the writer submit bytes that were never part of the payload.
        block_meta.block_size = payload_offset;
        if (!EnqueueBlockBuffer(block_meta.block_id, std::move(compress_buffer)))
        {
            return error_run_time("pack writer stopped before task {} was queued", block_meta.block_id);
        }
        return {};
    }

    Status BlockStore::UnPackTask(const DataBlockMeta& block_meta)
    {
        thread_local std::unique_ptr<ReadFile> read_file_ptr;
        thread_local RawBuffer tls_block_buf;
        if (!read_file_ptr)
        {
            auto read_file_status = NewPosixReadFile(packed_file_);
            if (!read_file_status)
            {
                return error_io("fail to open target file {}", packed_file_);
            }
            read_file_ptr = std::move(read_file_status.value());
        }
        uint64_t block_beg_pos = file_data_offset_ + block_meta.block_file_offset;
        tls_block_buf.EnsureCapacity(block_meta.block_size);
        struct TrimBlockBufferOnExit
        {
            RawBuffer& buffer;
            ~TrimBlockBufferOnExit() { buffer.TrimTo(kMaxRetainedBufferBytes); }
        } trim_block_buffer{tls_block_buf};

        ResultStatus<size_t> read_status =
            read_file_ptr->RandomRead(block_beg_pos, block_meta.block_size, tls_block_buf.Data());

        if (!read_status || read_status.value() != block_meta.block_size)
        {
            return error_io("failed to read data from {}", packed_file_);
        }

        size_t file_count = block_meta.file_ids.size();
        // The archive is untrusted: resolve every entry against the output directory
        // and refuse anything that would land outside it.
        const fs::path out_root(target_path_);
        const fs::path archive_dir(std::string(block_meta.dir_stats.dir_path));
        RawBuffer decompress_buffer;
        uint64_t hash_value;
        for (size_t i = 0; i < file_count; ++i)
        {
            entry_id_t fid = block_meta.file_ids[i];
            auto file_name_status = pack_file_handle_.GetFileNameByFileId(fid);
            if (!file_name_status)
            {
                return error_not_found("failed to find file name in block_{}", block_meta.block_id);
            }
            const uint64_t file_offset = block_meta.file_offsets[i];
            auto output_path = ResolveArchiveFile(out_root, archive_dir, file_name_status.value());
            if (!output_path)
            {
                return std::unexpected(output_path.error());
            }
            const std::string output_file_name = output_path.value().string();
            if (block_meta.file_orig_sizes[i] > 0)
            {
                if (max_decompressed_file_size_ > 0 &&
                    block_meta.file_orig_sizes[i] > max_decompressed_file_size_)
                {
                    return error_corruption("file '{}' exceeds the decompression budget of {} bytes",
                                            output_file_name, max_decompressed_file_size_);
                }
                decompress_buffer.EnsureCapacity(block_meta.file_orig_sizes[i]);
                auto decompress_size = compress::Decompress(
                    block_meta.file_compressed_types[i], tls_block_buf.Data() + file_offset, decompress_buffer.Data(),
                    block_meta.file_compressed_sizes[i], block_meta.file_orig_sizes[i]);
                if (decompress_size != block_meta.file_orig_sizes[i])
                {
                    return error_corruption("failed to decompress file: {}", output_file_name);
                }
                hash_value = XXH64(decompress_buffer.Data(), block_meta.file_orig_sizes[i], kHashSeed);
            }
            else
            {
                hash_value = kZeroXXHash;
            }
            if (hash_value != block_meta.file_hashes[i])
            {
                return error_corruption("the hash value of decompressed data is not equal to the original file: {}",
                                        output_file_name);
            }

            auto w_file_status = NewPosixWriteFile(output_file_name, true);
            if (!w_file_status)
            {
                return error_io("failed to create file: {} for write", output_file_name);
            }
            auto& w_file = w_file_status.value();
            Status write_status = w_file->Write(block_meta.file_orig_sizes[i], decompress_buffer.Data());
            if (!write_status)
            {
                return write_status;
            }
        }
        return {};
    }

    Status BlockStore::BuildBlocksFromFileStats()
    {
        uint32_t task_id = 0;
        const uint64_t total_file_cnt = pack_file_handle_.TotalFileCnt();
        if (total_file_cnt == 0)
        {
            return error_run_time("there are no files to pack, something may be wrong");
        }
        data_block_metas_.reserve(total_file_cnt / kMaxFileCountPerBlock + 1);
        for (const auto& dir_to_file_id : pack_file_handle_.GetDirIdToChildFileIds())
        {
            const auto& file_ids = dir_to_file_id.second;
            const size_t file_count = file_ids.size();
            if (file_count == 0)
                continue;
            const auto& file_sizes = pack_file_handle_.GetFileSizeByDirId(dir_to_file_id.first);
            auto dir_path_status = pack_file_handle_.GetDirPathByDirId(dir_to_file_id.first);
            if (!dir_path_status)
                return error_io("not found the path in pack file");
            size_t pos = 0;
            while (pos < file_count)
            {
                data_block_metas_.emplace_back();
                DataBlockMeta& block_meta = data_block_metas_.back();
                block_meta.dir_id = dir_to_file_id.first;
                block_meta.block_id = task_id++;
                block_meta.dir_stats.dir_path = dir_path_status.value();
                block_meta.file_ids.reserve(kMaxFileCountPerBlock);
                block_meta.file_orig_sizes.reserve(kMaxFileCountPerBlock);
                uint64_t accumulated_bytes = 0;
                size_t end_pos = pos;
                for (; end_pos < file_count; ++end_pos)
                {
                    const uint64_t sz = file_sizes[end_pos];
                    if (!block_meta.file_ids.empty() && accumulated_bytes + sz > kMaxFileSizePerBlock)
                    {
                        break;
                    }
                    if (block_meta.file_ids.size() >= kMaxFileCountPerBlock)
                    {
                        break;
                    }
                    block_meta.dir_stats.max_file_size = std::max(block_meta.dir_stats.max_file_size, sz);
                    block_meta.file_ids.push_back(file_ids[end_pos]);
                    block_meta.file_orig_sizes.push_back(sz);
                    accumulated_bytes += sz;
                }
                block_meta.file_compressed_sizes.resize(block_meta.file_ids.size());
                block_meta.file_compressed_types.resize(block_meta.file_ids.size());
                block_meta.file_hashes.resize(block_meta.file_ids.size());
                block_meta.file_offsets.resize(block_meta.file_ids.size());
                block_meta.block_size = accumulated_bytes;
                block_meta.block_file_offset = 0;
                // Upper bound of the block region: compression never expands a block,
                // so the uncompressed size is the worst case.
                max_block_region_size_ += block_meta.block_size;
                pos = end_pos;
            }
        }
        return {};
    }

    Status BlockStore::SubmitAndAwaitTasks(TaskOperation op)
    {
        thread_pool pool(thread_cnt_);
        std::vector<std::future<Status>> futures;
        futures.reserve(data_block_metas_.size());
        switch (op)
        {
        case TaskOperation::Pack:
            // NOLINTNEXTLINE(modernize-loop-convert): avoid reference‑capture use‑after‑scope UB
            for (size_t i = 0; i < data_block_metas_.size(); ++i)
            {
                futures.push_back(pool.submit([this, i] { return this->PackTask(data_block_metas_[i]); }));
            }
            break;
        case TaskOperation::Extract:
            {
                // NOLINTNEXTLINE(modernize-loop-convert): avoid reference‑capture use‑after‑scope UB
                for (size_t i = 0; i < data_block_metas_.size(); ++i)
                {
                    futures.push_back(pool.submit([this, i] { return this->UnPackTask(data_block_metas_[i]); }));
                }
            }
            break;
        default:
            return error_run_time("unknown task operation");
        }
        std::optional<ErrorStatus> first_err;
        for (size_t i = 0; i < futures.size(); ++i)
        {
            try
            {
                auto result = futures[i].get();
                if (!result && !first_err.has_value())
                {
                    std::println(std::cerr, "Task {} failed: {}", i, result.error().message());
                    first_err = result.error();
                    pool.stop();
                    break;
                }
            }
            catch (const std::exception& e)
            {
                std::println(std::cerr, "Task {} unexpected exception: {}", i, e.what());
                if (!first_err)
                {
                    auto err_st = error_run_time("Task {} unexpected exception: {}", i, e.what());
                    first_err = err_st.error();
                    pool.stop();
                    break;
                }
            }
            catch (...)
            {
                std::println(std::cerr, "Task {} unknown exception", i);
                if (!first_err)
                {
                    auto err_st = error_run_time("Task {} unknown exception", i);
                    first_err = err_st.error();
                    pool.stop();
                    break;
                }
            }
        }
        pool.wait();
        if (first_err)
        {
            return std::unexpected(*first_err);
        }
        return {};
    }

    Status BlockStore::PackPipeline(const std::string& output_pack_file, bool force)
    {
        std::error_code exists_ec;
        if (!force && fs::exists(output_pack_file, exists_ec))
        {
            return error_io("output archive '{}' already exists; pass --force to overwrite it", output_pack_file);
        }
        auto write_file_status = NewPosixWriteFile(output_pack_file, true);
        if (!write_file_status)
        {
            return error_io("fail to open {}: {} ", output_pack_file, strerror(errno));
        }
        auto& write_file = write_file_status.value();
        block_region_size_ = 0;
        max_block_region_size_ = 0;
        file_data_compressed_size_ = 0;
        last_write_block_id_ = 0;
        data_block_metas_.clear();
        Status status = BuildBlocksFromFileStats();
        if (!status)
        {
            return status;
        }

        packed_file_ = output_pack_file;
        const uint64_t pack_begin_time = CurrentTimeMillis();
        const uint64_t raw_meta_size = GetRawMetaSize();
        const CompressionType file_data_type = compression_config_.compression_type;
        const int compression_param = compression_config_.compression_param;

        // metadata follows the file-data compression strategy, except that it is only
        // compressed when it reaches the threshold.
        const bool compress_metadata =
            file_data_type != CompressionType::kNone && raw_meta_size >= kMetadataCompressionThreshold;
        const CompressionType metadata_type = compress_metadata ? file_data_type : CompressionType::kNone;
        const uint64_t max_metadata_size =
            compress_metadata ? compress::MaxCompressBufferSize(metadata_type, raw_meta_size) : raw_meta_size;

        legacy_layout_ = false;
        file_data_offset_ = PackFileHeader::kSerializedSize;
        metadata_original_size_ = raw_meta_size;
        metadata_compressed_size_ = raw_meta_size;
        metadata_compressed_type_ = static_cast<uint64_t>(CompressionType::kNone);
        metadata_checksum_ = 0;
        const uint64_t max_file_size =
            file_data_offset_ + max_block_region_size_ + max_metadata_size + sizeof(kMagicNumber);
        if (!write_file->Preallocate(static_cast<size_t>(max_file_size)))
        {
            return error_io("fail to preallocate compressed file {}: {}", output_pack_file, strerror(errno));
        }
        {
            available_block_buffers_.clear();
            pending_write_items_.clear();
            writer_error_.reset();
            pack_write_done_ = false;
            for (size_t i = 0; i < kBlockBufferCount; ++i)
            {
                available_block_buffers_.push_back(std::make_unique<BlockBuffer>());
            }
        }
        std::thread writer([this] { WriterLoop(); });

        // Will block here until all tasks (pack or unpack, not include the task that write data to disk) complete or
        // any task fails.
        status = SubmitAndAwaitTasks(TaskOperation::Pack);

        {
            std::lock_guard lock(pack_queue_mutex_);
            pack_write_done_ = true;
        }
        //  only one writer thread need to notify
        write_ready_.notify_one();
        writer.join();

        if (!status)
        {
            fs::remove(output_pack_file);
            return status;
        }
        if (writer_error_)
        {
            fs::remove(output_pack_file);
            return std::unexpected(*writer_error_);
        }

        for (auto& task : data_block_metas_)
        {
            uint64_t index = 0;
            auto beg_pos = static_cast<off_t>(file_data_offset_ + task.block_file_offset);
            for (const entry_id_t& file_id : task.file_ids)
            {
                pack_file_handle_.SetFileEntryMeta(file_id, task.block_id, beg_pos + task.file_offsets[index],
                                                   task.file_compressed_sizes[index]);
                index += 1;
            }
        }
        pack_file_handle_.SetCompressedAndPaddingBytes(file_data_compressed_size_, block_region_size_);

        // Serialize the metadata (without a header; the header is fixed-size at the front).
        std::string meta_buffer;
        meta_buffer.reserve(static_cast<size_t>(raw_meta_size));
        pack_file_handle_.SerializePackFileMeta(&meta_buffer);
        PutFixed64(&meta_buffer, data_block_metas_.size());
        for (const auto& meta : data_block_metas_)
        {
            meta.Serialize(&meta_buffer);
        }
        if (meta_buffer.size() != raw_meta_size)
        {
            fs::remove(output_pack_file);
            return error_run_time("internal error: metadata size {} does not match the precomputed {}", meta_buffer.size(),
                                  raw_meta_size);
        }
        metadata_checksum_ = XXH64(meta_buffer.data(), meta_buffer.size(), kHashSeed);

        // The uncompressed bytes are written directly when metadata compression is
        // disabled or does not shrink. The compression destination is raw memory:
        // zero-filling a std::string of the worst-case size committed ~29 MiB of
        // metadata to RSS even though the compressed output was only ~7 MiB (#93).
        std::unique_ptr<char[]> compressed_meta;
        uint64_t compressed_size = 0;
        if (compress_metadata)
        {
            compressed_meta = std::make_unique_for_overwrite<char[]>(static_cast<size_t>(max_metadata_size));
            compressed_size =
                compress::Compress(metadata_type, meta_buffer.data(), compressed_meta.get(), raw_meta_size,
                                   compression_param);
        }
        const MetadataStoragePlan metadata_plan =
            PlanMetadataStorage(raw_meta_size, compressed_size, static_cast<uint64_t>(metadata_type));
        const char* stored_metadata = meta_buffer.data();
        if (metadata_plan.use_compressed)
        {
            stored_metadata = compressed_meta.get();
        }
        else
        {
            // Compression was not used or did not help; write the raw bytes.
            compressed_meta.reset();
        }
        const uint64_t stored_metadata_size = metadata_plan.stored_size;
        metadata_compressed_size_ = metadata_plan.header_size;
        metadata_compressed_type_ = metadata_plan.header_type;

        const uint64_t metadata_offset = file_data_offset_ + block_region_size_;
        const uint64_t pack_file_size = metadata_offset + metadata_compressed_size_ + sizeof(kMagicNumber);
        archive_size_ = pack_file_size;

        const auto file_stats = pack_file_handle_.GetFileStats();
        PackFileHeader header;
        header.magic_number = kMagicNumber;
        header.file_format_version = kFileFormatVersion;
        header.ctar_version = kToolVersion.Encode();
        header.compressed_param = static_cast<uint32_t>(compression_param);
        header.pack_begin_time = pack_begin_time;
        header.pack_end_time = CurrentTimeMillis();
        header.pack_file_size = pack_file_size;
        header.file_count = file_stats.file_count;
        header.directory_count = pack_file_handle_.GetDirectoryCount();
        header.block_count = data_block_metas_.size();
        header.file_data_offset = file_data_offset_;
        header.file_data_original_size = file_stats.file_data_original_size;
        header.file_data_compressed_size = file_data_compressed_size_;
        header.file_data_compressed_type = static_cast<uint64_t>(file_data_type);
        header.metadata_offset = metadata_offset;
        header.metadata_original_size = metadata_original_size_;
        header.metadata_compressed_size = metadata_compressed_size_;
        header.metadata_compressed_type = metadata_compressed_type_;
        header.metadata_checksum = metadata_checksum_;
        pack_file_header_ = header;

        std::string header_bytes;
        header.Serialize(&header_bytes);
        status = write_file->RandomWrite(0, header_bytes.size(), header_bytes.data());
        if (!status)
        {
            fs::remove(output_pack_file);
            return status;
        }
        if (stored_metadata_size > 0)
        {
            status = write_file->RandomWrite(static_cast<off_t>(metadata_offset),
                                              static_cast<size_t>(stored_metadata_size), stored_metadata);
            if (!status)
            {
                fs::remove(output_pack_file);
                return status;
            }
        }
        std::string magic_number;
        PutFixed32(&magic_number, kMagicNumber);
        status = write_file->RandomWrite(static_cast<off_t>(metadata_offset + metadata_compressed_size_),
                                         sizeof(kMagicNumber), magic_number.data());
        if (!status)
        {
            fs::remove(output_pack_file);
            return status;
        }
        if (!write_file->TruncFile(static_cast<off_t>(pack_file_size)))
        {
            fs::remove(output_pack_file);
            return error_io("fail to truncate compressed file {}: {}", output_pack_file, strerror(errno));
        }
        if (!write_file->Sync())
        {
            fs::remove(output_pack_file);
            return error_io("durable sync failed for archive {}: {}", output_pack_file, strerror(errno));
        }
        return {};
    }

    namespace
    {
        /// True when the directory tree contains at least one entry that is not a
        /// directory (regular file, symlink, ...). A destination holding only empty
        /// directories counts as empty.
        ResultStatus<bool> DirectoryContainsFiles(const fs::path& root)
        {
            std::error_code ec;
            fs::recursive_directory_iterator it(root, ec);
            if (ec)
            {
                return error_io("failed to read '{}': {}", root.string(), ec.message());
            }
            const fs::recursive_directory_iterator end;
            while (it != end)
            {
                std::error_code status_ec;
                const fs::file_status status = it->symlink_status(status_ec);
                if (status_ec)
                {
                    return error_io("failed to stat '{}': {}", it->path().string(), status_ec.message());
                }
                if (!fs::is_directory(status))
                {
                    return true;
                }
                it.increment(ec);
                if (ec)
                {
                    return error_io("failed to enumerate '{}': {}", root.string(), ec.message());
                }
            }
            return false;
        }

        /// create_directories() that remembers which directories it actually created,
        /// so a failed extraction can restore an empty destination exactly.
        Status CreateDirectoriesTracked(const fs::path& path, std::vector<fs::path>& created)
        {
            std::vector<fs::path> missing;
            for (fs::path p = path; !p.empty(); p = p.parent_path())
            {
                std::error_code ec;
                if (fs::exists(p, ec))
                {
                    break;
                }
                missing.push_back(p);
                if (p.parent_path() == p)
                {
                    break;
                }
            }
            for (auto it = missing.rbegin(); it != missing.rend(); ++it)
            {
                std::error_code ec;
                if (fs::create_directory(*it, ec))
                {
                    created.push_back(*it);
                }
                else if (ec && !fs::is_directory(*it))
                {
                    return error_io("failed to create dir {}: {}", it->string(), ec.message());
                }
            }
            return {};
        }

        /// Undo the files and directories created by a failed extraction. Only valid
        /// when the destination had no files before the run.
        void CleanupExtraction(const fs::path& root, const std::vector<fs::path>& created_dirs, bool remove_root)
        {
            std::error_code ec;
            if (remove_root)
            {
                fs::remove_all(root, ec);
                return;
            }
            // Every file inside belongs to this attempt; remove them, then the
            // directories this run created (deepest first). Directories that existed
            // before the run stay where they are.
            for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    return;
                }
                std::error_code type_ec;
                if (!it->is_directory(type_ec))
                {
                    std::error_code remove_ec;
                    fs::remove(it->path(), remove_ec);
                }
            }
            for (auto it = created_dirs.rbegin(); it != created_dirs.rend(); ++it)
            {
                std::error_code remove_ec;
                fs::remove(*it, remove_ec);
            }
        }
    } // namespace

    Status BlockStore::CreateUnpackDir()
    {
        const fs::path out_root(target_path_);
        for (const auto& dir_map : pack_file_handle_.GetDirEntryRecords())
        {
            auto dir_name_opt = pack_file_handle_.GetDirPathByDirId(dir_map.first);
            if (!dir_name_opt)
            {
                return error_io("cannot find dir name of id {}", dir_map.first);
            }
            auto full_path = ResolveArchiveDirectory(out_root, fs::path(dir_name_opt.value()));
            if (!full_path)
            {
                return std::unexpected(full_path.error());
            }
            if (track_created_dirs_)
            {
                if (auto status = CreateDirectoriesTracked(full_path.value(), created_dirs_); !status)
                {
                    return status;
                }
            }
            else
            {
                std::error_code ec;
                if (!fs::create_directories(full_path.value(), ec) && !fs::is_directory(full_path.value(), ec))
                {
                    return error_io("failed to create dir: {}", full_path.value().string());
                }
            }
        }
        return {};
    }

    Status BlockStore::ReadPackHeaderOnly(const std::string& pack_file_path)
    {
        packed_file_ = pack_file_path;
        auto read_file_status = NewPosixReadFile(pack_file_path);
        if (!read_file_status)
        {
            return error_io("fail to open file {}: {}", pack_file_path, strerror(errno));
        }
        auto& read_file = read_file_status.value();
        const auto file_size_status = read_file->GetFileSize();
        if (!file_size_status)
        {
            return error_io("fail to get the size of {}: {}", pack_file_path, file_size_status.error().message());
        }
        const uint64_t file_size = file_size_status.value();
        archive_size_ = file_size;
        if (file_size < LegacyPackFileHeader::kSerializedSize)
        {
            return error_corruption("'{}' is too small to be a ctar archive", pack_file_path);
        }

        std::string prefix(LegacyPackFileHeader::kSerializedSize, '\0');
        auto read_status = read_file->Read(prefix.size(), prefix.data());
        if (!read_status || read_status.value() != prefix.size())
        {
            return error_io("fail to read data from {}", pack_file_path);
        }
        const uint32_t magic = DecodeFixed32(prefix.data());
        if (magic != kMagicNumber)
        {
            return error_corruption("Invalid magic number");
        }
        const Version version = Version::Decode(DecodeFixed32(prefix.data() + sizeof(uint32_t)));
        if (version.major > kFileFormatVersion.major ||
            (version.major == kFileFormatVersion.major && version.minor > kFileFormatVersion.minor))
        {
            return error_corruption("archive format {} is newer than the supported {}",
                                    version.to_string(), kFileFormatVersion.to_string());
        }

        if (version.major == kFileFormatVersion.major && version.minor == kFileFormatVersion.minor)
        {
            // 1.1.0: fixed 4096-byte header, metadata at the end.
            if (file_size < PackFileHeader::kSerializedSize)
            {
                return error_corruption("'{}' is too small for a 1.1.0 archive", pack_file_path);
            }
            std::string header_bytes(PackFileHeader::kSerializedSize, '\0');
            read_status = read_file->RandomRead(0, header_bytes.size(), header_bytes.data());
            if (!read_status || read_status.value() != header_bytes.size())
            {
                return error_io("fail to read header from {}", pack_file_path);
            }
            PackFileHeader header;
            Status s = header.Deserialize(header_bytes.data(), header_bytes.data() + header_bytes.size());
            if (!s)
            {
                return s;
            }
            for (size_t i = PackFileHeader::kUsedSize; i < header_bytes.size(); ++i)
            {
                if (header_bytes[i] != 0)
                {
                    return error_corruption("corrupt archive '{}': header reserved bytes are not zero",
                                            pack_file_path);
                }
            }
            if (header.file_data_offset != PackFileHeader::kSerializedSize)
            {
                return error_corruption("corrupt archive '{}': file data offset {} is not {}", pack_file_path,
                                        header.file_data_offset, PackFileHeader::kSerializedSize);
            }
            if (header.metadata_offset != header.file_data_offset + header.file_data_compressed_size)
            {
                return error_corruption("corrupt archive '{}': metadata offset does not follow the file data",
                                        pack_file_path);
            }
            if (header.pack_file_size != file_size)
            {
                return error_corruption("corrupt archive '{}': header size {} does not match the file size {}",
                                        pack_file_path, header.pack_file_size, file_size);
            }
            if (header.file_data_compressed_size > file_size || header.metadata_offset > file_size ||
                file_size - header.metadata_offset < sizeof(kMagicNumber) ||
                header.metadata_compressed_size > file_size - header.metadata_offset - sizeof(kMagicNumber))
            {
                return error_corruption("corrupt archive '{}': metadata size {} exceeds the file size {}",
                                        pack_file_path, header.metadata_compressed_size, file_size);
            }
            // Bound the allocation for a maliciously large uncompressed metadata size.
            constexpr uint64_t kMaxMetadataSize = 4ULL * 1024 * 1024 * 1024;
            if (header.metadata_original_size == 0 || header.metadata_original_size > kMaxMetadataSize ||
                header.metadata_compressed_size == 0)
            {
                return error_corruption("corrupt archive '{}': impossible metadata sizes", pack_file_path);
            }
            if (header.metadata_compressed_type != static_cast<uint64_t>(CompressionType::kNone) &&
                header.metadata_original_size / header.metadata_compressed_size > 255)
            {
                return error_corruption("corrupt archive '{}': impossible metadata compression ratio", pack_file_path);
            }
            if (!IsValidCompressionType(header.file_data_compressed_type) ||
                !IsValidCompressionType(header.metadata_compressed_type))
            {
                return error_corruption("corrupt archive '{}': invalid compression type", pack_file_path);
            }
            if (header.metadata_compressed_type == static_cast<uint64_t>(CompressionType::kNone) &&
                header.metadata_compressed_size != header.metadata_original_size)
            {
                return error_corruption("corrupt archive '{}': uncompressed metadata size mismatch", pack_file_path);
            }
            legacy_layout_ = false;
            pack_file_header_ = header;
            file_data_offset_ = header.file_data_offset;
            block_region_size_ = header.file_data_compressed_size;
            metadata_original_size_ = header.metadata_original_size;
            metadata_compressed_size_ = header.metadata_compressed_size;
            metadata_compressed_type_ = header.metadata_compressed_type;
            metadata_checksum_ = header.metadata_checksum;
            return {};
        }

        // 1.0.0 and older: 16-byte header, metadata follows it.
        const char* ptr = prefix.data();
        LegacyPackFileHeader header;
        Status s = header.Deserialize(ptr, prefix.data() + prefix.size());
        if (!s)
        {
            return s;
        }
        if (header.file_meta_size > file_size - LegacyPackFileHeader::kSerializedSize)
        {
            return error_corruption("corrupt archive '{}': metadata size {} exceeds the file size {}", pack_file_path,
                                    header.file_meta_size, file_size);
        }
        legacy_layout_ = true;
        pack_file_header_ = PackFileHeader{};
        legacy_file_meta_size_ = header.file_meta_size;
        meta_region_size_ = LegacyPackFileHeader::kSerializedSize + header.file_meta_size;
        file_data_offset_ = meta_region_size_;
        metadata_original_size_ = header.file_meta_size;
        metadata_compressed_size_ = header.file_meta_size;
        metadata_compressed_type_ = static_cast<uint64_t>(CompressionType::kNone);
        metadata_checksum_ = 0;
        block_region_size_ = file_size - meta_region_size_;
        return {};
    }

    Status BlockStore::RestoreBlocksFromPack(const std::string& pack_file_path,
                                             const uint64_t max_decompressed_file_size)
    {
        Status s = ReadPackHeaderOnly(pack_file_path);
        if (!s)
        {
            return s;
        }
        auto read_file_status = NewPosixReadFile(pack_file_path);
        if (!read_file_status)
        {
            return error_io("fail to open file {}: {}", pack_file_path, strerror(errno));
        }
        auto& read_file = read_file_status.value();

        std::string metadata_bytes;
        if (legacy_layout_)
        {
            metadata_bytes.assign(static_cast<size_t>(legacy_file_meta_size_), '\0');
            auto read_status =
                read_file->RandomRead(static_cast<off_t>(LegacyPackFileHeader::kSerializedSize),
                                      legacy_file_meta_size_, metadata_bytes.data());
            if (!read_status || read_status.value() != static_cast<size_t>(legacy_file_meta_size_))
            {
                return error_io("fail to read metadata from {}", pack_file_path);
            }
        }
        else
        {
            std::string stored(static_cast<size_t>(metadata_compressed_size_), '\0');
            if (!stored.empty())
            {
                auto read_status = read_file->RandomRead(static_cast<off_t>(pack_file_header_.metadata_offset),
                                                         metadata_compressed_size_, stored.data());
                if (!read_status || read_status.value() != static_cast<size_t>(metadata_compressed_size_))
                {
                    return error_io("fail to read metadata from {}", pack_file_path);
                }
            }
            const auto metadata_type = static_cast<CompressionType>(metadata_compressed_type_);
            if (metadata_type == CompressionType::kNone)
            {
                metadata_bytes = std::move(stored);
            }
            else
            {
                metadata_bytes.assign(static_cast<size_t>(metadata_original_size_), '\0');
                const uint64_t decompressed_size =
                    compress::Decompress(metadata_type, stored.data(), metadata_bytes.data(),
                                         metadata_compressed_size_, metadata_original_size_);
                if (decompressed_size != metadata_original_size_)
                {
                    return error_corruption("corrupt archive '{}': failed to decompress metadata", pack_file_path);
                }
            }
            const uint64_t checksum = XXH64(metadata_bytes.data(), metadata_bytes.size(), kHashSeed);
            if (checksum != metadata_checksum_)
            {
                return error_corruption("corrupt archive '{}': metadata checksum mismatch", pack_file_path);
            }
        }

        const char* meta_ptr = metadata_bytes.data();
        const char* const meta_end = metadata_bytes.data() + metadata_bytes.size();
        s = pack_file_handle_.DeserializePackFileMeta(meta_ptr, meta_end);
        if (!s)
        {
            return s;
        }
        if (static_cast<size_t>(meta_end - meta_ptr) < sizeof(uint64_t))
        {
            return error_corruption("corrupt archive '{}': missing data block count", pack_file_path);
        }
        const auto data_block_count = DecodeFixed64(meta_ptr);
        meta_ptr += sizeof(uint64_t);
        constexpr uint64_t kMinDataBlockMetaSize = 12 * sizeof(uint64_t);
        if (data_block_count > static_cast<uint64_t>(meta_end - meta_ptr) / kMinDataBlockMetaSize)
        {
            return error_corruption("corrupt archive '{}': data block count {} is impossible", pack_file_path,
                                    data_block_count);
        }
        // The metadata index is deserialized before the block list, so a corrupt block
        // can name the offending archive path instead of an internal id.
        const auto entry_path = [this](entry_id_t dir_id, entry_id_t file_id) -> std::string
        {
            const auto file_name = pack_file_handle_.GetFileNameByFileId(file_id);
            if (!file_name)
            {
                return {};
            }
            const auto dir_path = pack_file_handle_.GetDirPathByDirId(dir_id);
            if (!dir_path || dir_path.value().empty())
            {
                return std::string(file_name.value());
            }
            return std::format("{}/{}", dir_path.value(), file_name.value());
        };

        data_block_metas_.clear();
        data_block_metas_.resize(static_cast<size_t>(data_block_count));
        // Cross-block and cross-index checks. Together with the per-block range checks
        // this prevents a hostile archive from reusing the same compressed bytes, or the
        // same output path, through several block entries.
        std::vector<std::pair<uint64_t, uint64_t>> block_ranges;
        block_ranges.reserve(data_block_metas_.size());
        std::unordered_set<uint64_t> block_ids;
        const auto& file_records = pack_file_handle_.GetFileEntryRecords();
        std::vector<std::pair<entry_id_t, std::string_view>> file_names;
        file_names.reserve(file_records.size());
        for (size_t i = 0; i < data_block_metas_.size(); ++i)
        {
            s = data_block_metas_[i].Deserialize(meta_ptr, meta_end);
            if (!s)
            {
                return s;
            }
            const auto& block_meta = data_block_metas_[i];
            if (block_meta.block_size > block_region_size_ ||
                block_meta.block_file_offset > block_region_size_ - block_meta.block_size)
            {
                return error_corruption("corrupt archive '{}': data block {} lies outside the block region",
                                        pack_file_path, block_meta.block_id);
            }
            if (auto valid = ValidateDataBlockMeta(block_meta, entry_path, max_decompressed_file_size); !valid)
            {
                return error_corruption("corrupt archive '{}': {}", pack_file_path, valid.error().message());
            }
            if (!block_ids.insert(block_meta.block_id).second)
            {
                return error_corruption("corrupt archive '{}': duplicate data block id {}", pack_file_path,
                                        block_meta.block_id);
            }
            block_ranges.emplace_back(block_meta.block_file_offset,
                                      block_meta.block_file_offset + block_meta.block_size);
            if (auto dir_path = pack_file_handle_.GetDirPathByDirId(block_meta.dir_id))
            {
                data_block_metas_[i].dir_stats.dir_path = dir_path.value();
            }
            else
            {
                return error_io("fail to find the dir name of {}", block_meta.dir_id);
            }
        }
        std::ranges::sort(block_ranges);
        for (size_t i = 1; i < block_ranges.size(); ++i)
        {
            if (block_ranges[i].first < block_ranges[i - 1].second)
            {
                return error_corruption("corrupt archive '{}': data blocks overlap in the block region",
                                        pack_file_path);
            }
        }
        // The entry index is what ls/dump display and what UnPackTask resolves the name
        // from; it has to describe exactly the same file as every block entry.
        uint64_t referenced_files = 0;
        for (const auto& block_meta : data_block_metas_)
        {
            for (size_t f = 0; f < block_meta.file_ids.size(); ++f)
            {
                const entry_id_t file_id = block_meta.file_ids[f];
                const auto record = file_records.find(file_id);
                if (record == file_records.end())
                {
                    return error_corruption("corrupt archive '{}': data block {} references unknown file id {}",
                                            pack_file_path, block_meta.block_id, file_id);
                }
                const EntryMeta& entry = record->second.entry_meta;
                const uint64_t absolute_offset =
                    file_data_offset_ + block_meta.block_file_offset + block_meta.file_offsets[f];
                if (entry.block_id != block_meta.block_id || entry.file_orig_size != block_meta.file_orig_sizes[f] ||
                    entry.file_compressed_size != block_meta.file_compressed_sizes[f] ||
                    entry.offset != absolute_offset)
                {
                    return error_corruption(
                        "corrupt archive '{}': file '{}' in data block {} disagrees with the entry index",
                        pack_file_path, entry_path(block_meta.dir_id, file_id), block_meta.block_id);
                }
                const auto file_name = pack_file_handle_.GetFileNameByFileId(file_id);
                if (!file_name)
                {
                    return error_corruption("corrupt archive '{}': file id {} has no entry name", pack_file_path,
                                            file_id);
                }
                file_names.emplace_back(block_meta.dir_id, file_name.value());
                ++referenced_files;
            }
        }
        if (referenced_files != static_cast<uint64_t>(file_records.size()))
        {
            return error_corruption("corrupt archive '{}': entry index has {} files but blocks reference {}",
                                    pack_file_path, file_records.size(), referenced_files);
        }
        // The output path is the stored directory path plus the file name. A crafted
        // archive must not be able to make two entries resolve to the same path.
        std::vector<fs::path> dir_paths;
        dir_paths.reserve(pack_file_handle_.GetDirectoryCount());
        for (const auto& dir_entry : pack_file_handle_.GetDirEntryRecords())
        {
            const auto dir_path = pack_file_handle_.GetDirPathByDirId(dir_entry.first);
            if (!dir_path)
            {
                return error_corruption("corrupt archive '{}': directory {} has no entry name", pack_file_path,
                                        dir_entry.first);
            }
            dir_paths.push_back(fs::path(std::string(dir_path.value())).relative_path().lexically_normal());
        }
        std::ranges::sort(dir_paths);
        for (size_t i = 1; i < dir_paths.size(); ++i)
        {
            if (dir_paths[i] == dir_paths[i - 1])
            {
                return error_corruption("corrupt archive '{}': duplicate directory path '{}'", pack_file_path,
                                        dir_paths[i].string());
            }
        }
        std::ranges::sort(file_names);
        for (size_t i = 1; i < file_names.size(); ++i)
        {
            if (file_names[i].first == file_names[i - 1].first && file_names[i].second == file_names[i - 1].second)
            {
                return error_corruption("corrupt archive '{}': directory {} contains duplicate file name '{}'",
                                        pack_file_path, file_names[i].first, file_names[i].second);
            }
        }
        if (!legacy_layout_)
        {
            const auto stats = pack_file_handle_.GetFileStats();
            if (stats.file_count != pack_file_header_.file_count ||
                stats.file_data_original_size != pack_file_header_.file_data_original_size ||
                stats.file_data_compressed_size != pack_file_header_.file_data_compressed_size ||
                stats.block_padding_size != 0 ||
                data_block_metas_.size() != pack_file_header_.block_count ||
                pack_file_handle_.GetDirectoryCount() != pack_file_header_.directory_count)
            {
                return error_corruption("corrupt archive '{}': header summary does not match the metadata",
                                        pack_file_path);
            }
        }
        return {};
    }

    Status BlockStore::UnPackPipeline(const std::string& input_file, const std::string& output_dir, bool force)
    {
        Status status = RestoreBlocksFromPack(input_file, max_decompressed_file_size_);
        if (!status)
        {
            return status;
        }

        const fs::path out_root(output_dir);
        std::error_code ec;
        const bool target_missing = !fs::exists(out_root, ec);
        bool target_had_files = false;
        if (!target_missing)
        {
            if (!fs::is_directory(out_root, ec))
            {
                return error_io("output path '{}' is not a directory", output_dir);
            }
            auto has_files = DirectoryContainsFiles(out_root);
            if (!has_files)
            {
                return std::unexpected(has_files.error());
            }
            target_had_files = has_files.value();
            if (target_had_files && !force)
            {
                return error_io("output directory '{}' is not empty; pass --force to overwrite it", output_dir);
            }
        }

        created_dirs_.clear();
        track_created_dirs_ = !target_missing;
        target_path_ = output_dir;
        status = CreateUnpackDir();
        if (status)
        {
            status = SubmitAndAwaitTasks(TaskOperation::Extract);
        }
        if (!status && !target_had_files)
        {
            // The destination had no files before this run, so everything inside it is
            // ours: remove it and leave the destination the way we found it.
            CleanupExtraction(out_root, created_dirs_, target_missing);
        }
        return status;
    }

    uint64_t BlockStore::GetRawMetaSize() const
    {
        size_t pack_file_meta_size = pack_file_handle_.PackFileMetaSerializedSize();
        size_t data_block_meta_size = 0;
        for (const auto& data_block_meta : data_block_metas_)
        {
            data_block_meta_size += data_block_meta.SerializedSize();
        }
        // use an uint64_t to store the count of blocks
        data_block_meta_size += sizeof(uint64_t);
        return pack_file_meta_size + data_block_meta_size;
    }
}; // namespace ctar
