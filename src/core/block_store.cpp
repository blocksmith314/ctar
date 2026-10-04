#include <expected>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <map>
#include <ranges>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>
#include <xxhash.h>

#include "block_store.h"
#include "compress.h"
#include "file_handle.h"
#include "src/utils/logger.h"
#include "src/utils/status.h"

namespace ctar
{
#ifndef O_BINARY
#define O_BINARY 0
#endif


    ResultStatus<uint64_t> Hash(const std::string& file_path)
    {
        auto read_file_status = NewPosixReadFile(file_path);
        if (!read_file_status)
        {
            return std::unexpected(read_file_status.error());
        }
        auto& read_file = read_file_status.value();
        uint64_t file_size = 0;
        if (auto s = read_file->GetFileSize(); !s)
        {
            file_size = s.value();
        }
        else
        {
            return std::unexpected(s.error());
        }
        std::vector<char> buf(file_size);
        auto read_status = read_file->Read(file_size, buf.data());
        if (!read_status || read_status.value() != file_size)
        {
            return error_io("failed to read data from {}", file_path);
        }
        return XXH64(buf.data(), buf.size(), kHashSeed);
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
            auto s = pack_file_handle_.GetDirIdByDirPath(target_dir);
            if (s)
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
            auto s = pack_file_handle_.GetDirIdByDirPath(target_dir);
            if (s)
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
        buffer->data.clear();
        if (buffer->data.capacity() > kMaxFileSizePerBlock)
        {
            std::string().swap(buffer->data);
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
                buffer->data.clear();
                if (buffer->data.capacity() > kMaxFileSizePerBlock)
                {
                    std::string().swap(buffer->data);
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
                size_t batch_end = std::min(pos + kMaxBatchWriteEntries, total);
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
                    total_compressed_size_ += std::accumulate(task.file_compressed_sizes.begin(),
                                                              task.file_compressed_sizes.end(), size_t{0});
                    iovs.push_back({.iov_base = pending.buffer->data.data(), .iov_len = task.block_size});
                    block_region_size_ += task.block_size;
                }
                Status write_status =
                    w_file->RandomWriteV(iovs.data(), static_cast<int>(iovs.size()), meta_region_size_ + batch_start);
                if (!write_status)
                {
                    batch_end = total;
                    std::lock_guard lock(pack_queue_mutex_);
                    writer_error_ = error_io("failed to write block to file").error();
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
        thread_local std::vector<char> tls_read_buf;
        tls_read_buf.clear();
        tls_read_buf.reserve(block_meta.dir_stats.max_file_size);
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
        compress_buffer->data.clear();
        auto compress_buffer_size =
            compress::MaxCompressBufferSize(compression_config_.compression_type, block_meta.block_size);
        compress_buffer->data.reserve(static_cast<size_t>(compress_buffer_size));

        uint64_t payload_offset = 0;
        size_t file_count = block_meta.file_ids.size();
        for (size_t i = 0; i < file_count; ++i)
        {
            entry_id_t fid = block_meta.file_ids[i];
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
                continue;
            }
            if (auto read_file_status = NewPosixReadFile(file_path))
            {
                auto& rf = read_file_status.value();
                auto read_status = rf->Read(original_size, tls_read_buf.data());
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
            bool skip_compression = ShouldSkipCompression(file_name_status.value());
            CompressionType compression_type;

            const bool can_compress = !skip_compression && original_size <= kMaxCompressFileSize;
            compression_type = compression_config_.compression_type;
            block_meta.file_hashes[i] = XXH64(tls_read_buf.data(), original_size, kHashSeed);
            uint64_t compressed_size = 0;
            if (can_compress)
            {
                compressed_size = compress::Compress(compression_type, tls_read_buf.data(),
                                                     compress_buffer->data.data() + payload_offset, original_size,
                                                     compression_config_.compression_param);
            }
            // If the input data cannot be compressed, or the compressed size exceeds the original size,
            // copy the raw data directly to the target buffer instead.
            if (compressed_size >= original_size || skip_compression)
            {
                compressed_size = compress::Compress(CompressionType::kNone, tls_read_buf.data(),
                                                     compress_buffer->data.data() + payload_offset, original_size);
                compression_type = CompressionType::kNone;
            }
            block_meta.file_offsets[i] = payload_offset;
            block_meta.file_compressed_sizes[i] = compressed_size;
            block_meta.file_compressed_types[i] = compression_type;
            payload_offset += compressed_size;
        }
        block_meta.payload_size = payload_offset;
        block_meta.block_size = next_4k_align(payload_offset);
        if (!EnqueueBlockBuffer(block_meta.block_id, std::move(compress_buffer)))
        {
            return error_run_time("pack writer stopped before task {} was queued", block_meta.block_id);
        }
        return {};
    }

    Status BlockStore::UnPackTask(const DataBlockMeta& block_meta)
    {
        thread_local std::unique_ptr<ReadFile> read_file_ptr;
        thread_local std::vector<char> tls_block_buf;
        tls_block_buf.clear();
        if (!read_file_ptr)
        {
            auto read_file_status = NewPosixReadFile(packed_file_);
            if (!read_file_status)
            {
                return error_io("fail to open target file {}", packed_file_);
            }
            read_file_ptr = std::move(read_file_status.value());
        }
        uint64_t block_beg_pos = meta_region_size_ + block_meta.block_file_offset;
        tls_block_buf.reserve(block_meta.block_size);

        ResultStatus<size_t> read_status =
            read_file_ptr->RandomRead(block_beg_pos, block_meta.block_size, tls_block_buf.data());

        if (!read_status || read_status.value() != block_meta.block_size)
        {
            return error_io("failed to read data from {}", packed_file_);
        }

        size_t file_count = block_meta.file_ids.size();
        fs::path output_dir =
            fs::path(target_path_) / fs::path(std::string(block_meta.dir_stats.dir_path)).relative_path();
        std::string decompress_buffer;
        for (size_t i = 0; i < file_count; ++i)
        {
            entry_id_t fid = block_meta.file_ids[i];
            auto file_name_status = pack_file_handle_.GetFileNameByFileId(fid);
            if (!file_name_status)
            {
                return error_not_found("failed to find file name in block_{}", block_meta.block_id);
            }
            const uint64_t file_offset = block_meta.file_offsets[i];
            std::string output_file_name = output_dir / std::string(file_name_status.value());
            decompress_buffer.reserve(block_meta.file_orig_sizes[i]);
            auto decompress_size = compress::Decompress(
                block_meta.file_compressed_types[i], tls_block_buf.data() + file_offset, decompress_buffer.data(),
                block_meta.file_compressed_sizes[i], block_meta.file_orig_sizes[i]);

            if (decompress_size != block_meta.file_orig_sizes[i])
            {
                return error_corruption("failed to decompress file: {}", output_file_name);
            }

            auto hash_value = XXH64(decompress_buffer.data(), block_meta.file_orig_sizes[i], kHashSeed);
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
            Status write_status = w_file->Write(block_meta.file_orig_sizes[i], decompress_buffer.data());
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
                // use for set the original size of pack file
                max_block_region_size_ += next_4k_align(block_meta.block_size);
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

    Status BlockStore::PackPipeline(const std::string& output_pack_file)
    {
        auto write_file_status = NewPosixWriteFile(output_pack_file, true);
        if (!write_file_status)
        {
            return error_io("fail to open {}: {} ", output_pack_file, strerror(errno));
        }
        auto& write_file = write_file_status.value();
        Status status = BuildBlocksFromFileStats();
        if (!status)
        {
            return status;
        }

        packed_file_ = output_pack_file;
        auto raw_meta_size = GetMetaSize();
        meta_region_size_ = next_4k_align(raw_meta_size);
        if (!write_file->TruncFile(static_cast<off_t>(meta_region_size_ + max_block_region_size_)))
        {
            return error_io("fail to truncate compressed file {}: {}", output_pack_file, strerror(errno));
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



        if (block_region_size_ < max_block_region_size_)
        {
            if (!write_file->TruncFile(static_cast<off_t>(meta_region_size_ + block_region_size_)))
            {
                fs::remove(output_pack_file);
                return error_io("fail to truncate compressed file {}: {}", output_pack_file, strerror(errno));
            }
        }
        else
        {
            block_region_size_ = max_block_region_size_;
        }
        std::string magic_number;
        magic_number.reserve(sizeof(MagicNumber));
        PutFixed32(&magic_number,MagicNumber);
        status = write_file->RandomWrite(meta_region_size_ + block_region_size_,sizeof(MagicNumber), magic_number.data());
        if (!status)
        {
            fs::remove(output_pack_file);
            return status;
        }

        for (auto& task : data_block_metas_)
        {
            uint64_t index = 0;
            auto beg_pos = static_cast<off_t>(meta_region_size_ + task.block_file_offset);
            for (const entry_id_t& file_id : task.file_ids)
            {
                pack_file_handle_.SetFileEntryMeta(file_id, task.block_id, beg_pos + task.file_offsets[index],
                                                   task.file_compressed_sizes[index]);
                index += 1;
            }
        }
        pack_file_handle_.SetCompressedAndPaddingBytes(total_compressed_size_, block_region_size_);
        std::string buffer;
        buffer.reserve(meta_region_size_);
        const uint64_t file_meta_size = meta_region_size_ - sizeof(PackFileHeader);
        pack_file_header_.file_meta_size = file_meta_size;
        // serialize meta 1: PackFileHeader
        pack_file_header_.Serialize(&buffer);
        // serialize meta 2: PackFileHandle
        pack_file_handle_.SerializePackFileMeta(&buffer);
        // serialize meta 3: count of data block
        PutFixed64(&buffer, data_block_metas_.size());
        // serialize meta 4: DataBlockMeta
        for (const auto& meta : data_block_metas_)
        {
            meta.Serialize(&buffer);
        }
        const auto total_size = static_cast<ssize_t>(meta_region_size_);

        status = write_file->Write(total_size, buffer.data());
        if (!status)
        {
            fs::remove(output_pack_file);
            return status;
        }
        if (!write_file->Sync())
        {
            fs::remove(output_pack_file);
            return error_io("durable sync failed for archive {}: {}", output_pack_file, strerror(errno));
        }
        return {};
    }

    Status BlockStore::CreateUnpackDir() const
    {
        for (const auto& dir_map : pack_file_handle_.GetDirEntryRecords())
        {
            auto dir_name_opt = pack_file_handle_.GetDirPathByDirId(dir_map.first);
            if (!dir_name_opt)
            {
                return error_io("cannot find dir name of id {}", dir_map.first);
            }
            fs::path sub_path = dir_name_opt.value();
            auto full_path = fs::path(target_path_) / sub_path.relative_path();
            if (auto success = fs::create_directories(full_path); !success && !fs::exists(full_path))
            {
                return error_io("failed to create dir: {}", full_path.string());
            }
        }
        return {};
    }

    Status BlockStore::RestoreBlocksFromPack(const std::string& pack_file_path)
    {
        packed_file_ = pack_file_path;
        auto read_file_status = NewPosixReadFile(pack_file_path);
        if (!read_file_status)
        {
            return error_io("fail to open file {}: {}", pack_file_path, strerror(errno));
        }
        auto& read_file = read_file_status.value();
        Status s;
        std::string header(sizeof(PackFileHeader), '\0');
        size_t header_size = header.size();
        auto read_status = read_file->Read(header_size, header.data());
        if (!read_status || read_status.value() != header_size)
        {
            return error_io("fail to read data from {}", pack_file_path);
        }
        const char* ptr = header.data();
        PackFileHeader file_header{};
        s = file_header.Deserialize(ptr);
        if (!s)
        {
            return s;
        }
        meta_region_size_ = file_header.file_meta_size + sizeof(PackFileHeader);
        std::string file_meta(file_header.file_meta_size, '\0');
        read_status =
            read_file->RandomRead(static_cast<off_t>(header_size), file_header.file_meta_size, file_meta.data());
        if (!read_status || read_status.value() != static_cast<size_t>(file_header.file_meta_size))
        {
            return error_io("fail to read data from {}: {}", pack_file_path, strerror(errno));
        }
        ptr = file_meta.data();
        s = pack_file_handle_.DeserializePackFileMeta(ptr);
        if (!s)
        {
            return s;
        }
        uint64_t data_block_count = DecodeFixed64(ptr);
        ptr += sizeof(uint64_t);
        data_block_metas_.resize(data_block_count);
        for (size_t i = 0; i < data_block_count; ++i)
        {
            s = data_block_metas_[i].Deserialize(ptr);
            if (s)
            {
                if (auto dir_path = pack_file_handle_.GetDirPathByDirId(data_block_metas_[i].dir_id))
                {
                    data_block_metas_[i].dir_stats.dir_path = dir_path.value();
                }
                else
                {
                    return error_io("fail to find the dir name of {}", data_block_metas_[i].dir_id);
                }
            }
            else
            {
                return s;
            }
        }
        return {};
    }

    Status BlockStore::UnPackPipeline(const std::string& input_file, const std::string& output_dir)
    {
        Status status = RestoreBlocksFromPack(input_file);
        if (!status)
        {
            return status;
        }
        target_path_ = output_dir;
        status = CreateUnpackDir();
        if (!status)
        {
            return status;
        }
        status = SubmitAndAwaitTasks(TaskOperation::Extract);
        if (!status)
        {
            return status;
        }
        return {};
    }

    uint64_t BlockStore::GetMetaSize() const
    {
        size_t header_size = pack_file_header_.SerializedSize();
        size_t pack_file_meta_size = pack_file_handle_.PackFileMetaSerializedSize();
        size_t data_block_meta_size = 0;
        for (const auto& data_block_meta : data_block_metas_)
        {
            data_block_meta_size += data_block_meta.SerializedSize();
        }
        // use a uint64_t to store the count of blocks
        data_block_meta_size += sizeof(uint64_t);
        return header_size + pack_file_meta_size + data_block_meta_size;
    }
}; // namespace ctar
