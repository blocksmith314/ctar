#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <cstring>

#include "file_handle.h"


namespace ctar
{

    namespace
    {
        struct FdGuard
        {
            int fd = -1;
            FdGuard() = default;
            explicit FdGuard(int fd) : fd(fd) {}

            FdGuard(FdGuard&& other) noexcept : fd(other.fd) { other.fd = -1; }

            FdGuard& operator=(FdGuard&& other) noexcept
            {
                if (this != &other)
                {
                    if (fd >= 0)
                    {
                        ::close(fd);
                    }
                    fd = other.fd;
                    other.fd = -1;
                }
                return *this;
            }

            FdGuard(const FdGuard&) = delete;
            FdGuard& operator=(const FdGuard&) = delete;

            ~FdGuard()
            {
                if (fd >= 0)
                {
                    ::close(fd);
                }
            }
        };

        inline bool SyncFd(int fd)
        {
#ifdef __APPLE__
            return ::fcntl(fd, F_FULLFSYNC) == 0;
#else
            return ::fdatasync(fd) == 0;
#endif
        }

    } // anonymous namespace

    namespace detail
    {
        namespace
        {
            /// Number of leading iovecs that fit into one syscall of at most @p max_bytes
            /// bytes and @p max_iov entries. The first entry is always accepted, even when
            /// it is larger than @p max_bytes; the caller slices that one instead.
            size_t PlanIovBatch(const std::vector<struct iovec>& iovs, const size_t max_bytes, const int max_iov)
            {
                size_t count = 0;
                size_t bytes = 0;
                while (count < iovs.size() && count < static_cast<size_t>(max_iov))
                {
                    if (count > 0 && bytes + iovs[count].iov_len > max_bytes)
                    {
                        break;
                    }
                    bytes += iovs[count].iov_len;
                    ++count;
                    if (bytes >= max_bytes)
                    {
                        break;
                    }
                }
                return count;
            }

            /// Consume the first @p written bytes from @p iovs. preadv/pwritev always
            /// transfer a prefix of the concatenated buffers, so the head is the only
            /// place where an entry can be partially consumed.
            void ConsumeIovPrefix(std::vector<struct iovec>& iovs, size_t written)
            {
                for (auto& iv : iovs)
                {
                    if (written == 0)
                    {
                        break;
                    }
                    if (iv.iov_len <= written)
                    {
                        written -= iv.iov_len;
                        iv.iov_base = nullptr;
                        iv.iov_len = 0;
                    }
                    else
                    {
                        iv.iov_base = static_cast<char*>(iv.iov_base) + written;
                        iv.iov_len -= written;
                        written = 0;
                    }
                }
                std::erase_if(iovs, [](const struct iovec& iv) { return iv.iov_len == 0; });
            }

            bool HasIoData(const std::vector<struct iovec>& iovs)
            {
                return std::any_of(iovs.begin(), iovs.end(), [](const struct iovec& iv) { return iv.iov_len > 0; });
            }
        } // anonymous namespace

        Status PwritevAll(int fd, const std::string& file_name, std::vector<struct iovec>& iovs, const uint64_t offset,
                          const size_t max_bytes, const int max_iov)
        {
            // writev() with a zero-length total is a no-op, not an error. Blocks whose
            // files are all empty reach this path with a single zero-length iovec.
            if (!HasIoData(iovs))
            {
                return {};
            }

            off_t cur_off = static_cast<off_t>(offset);
            while (!iovs.empty())
            {
                const size_t batch_cnt = PlanIovBatch(iovs, max_bytes, max_iov);
                if (batch_cnt == 0)
                {
                    break;
                }

                // Shrink the tail of the batch when a single entry exceeds what one
                // syscall may carry, then restore it so the consume step stays exact.
                const size_t entry_len = iovs[batch_cnt - 1].iov_len;
                const bool sliced = entry_len > max_bytes;
                if (sliced)
                {
                    iovs[batch_cnt - 1].iov_len = max_bytes;
                }

                ssize_t ret;
                do
                {
                    ret = pwritev(fd, iovs.data(), static_cast<int>(batch_cnt), cur_off);
                }
                while (ret < 0 && errno == EINTR);

                if (sliced)
                {
                    iovs[batch_cnt - 1].iov_len = entry_len;
                }

                if (ret < 0)
                {
                    return error_io("fail to pwritev {}: {}", file_name, strerror(errno));
                }
                if (ret == 0)
                {
                    return error_io("pwritev made no progress on {}", file_name);
                }

                cur_off += ret;
                ConsumeIovPrefix(iovs, static_cast<size_t>(ret));
            }
            return {};
        }

        ResultStatus<size_t> PreadvAll(int fd, const std::string& file_name, std::vector<struct iovec>& iovs,
                                       const uint64_t offset, const size_t max_bytes, const int max_iov)
        {
            off_t cur_off = static_cast<off_t>(offset);
            size_t total_read = 0;

            while (!iovs.empty())
            {
                const size_t batch_cnt = PlanIovBatch(iovs, max_bytes, max_iov);
                if (batch_cnt == 0)
                {
                    break;
                }

                const size_t entry_len = iovs[batch_cnt - 1].iov_len;
                const bool sliced = entry_len > max_bytes;
                if (sliced)
                {
                    iovs[batch_cnt - 1].iov_len = max_bytes;
                }

                ssize_t ret;
                do
                {
                    ret = preadv(fd, iovs.data(), static_cast<int>(batch_cnt), cur_off);
                }
                while (ret < 0 && errno == EINTR);

                if (sliced)
                {
                    iovs[batch_cnt - 1].iov_len = entry_len;
                }

                if (ret < 0)
                {
                    return error_io("fail to preadv {}: {}", file_name, strerror(errno));
                }
                if (ret == 0)
                {
                    break; // EOF
                }

                total_read += static_cast<size_t>(ret);
                cur_off += ret;
                ConsumeIovPrefix(iovs, static_cast<size_t>(ret));
            }
            return total_read;
        }

        ResultStatus<size_t> ReadAll(int fd, const std::string& file_name, const uint64_t offset,
                                     const bool positioned, const size_t read_size, char* read_buffer,
                                     const size_t max_bytes)
        {
            size_t total_read = 0;
            uint64_t cur_off = offset;

            while (total_read < read_size)
            {
                const size_t want = std::min(read_size - total_read, max_bytes);
                ssize_t ret;
                do
                {
                    ret = positioned ? pread(fd, read_buffer + total_read, want, static_cast<off_t>(cur_off))
                                     : read(fd, read_buffer + total_read, want);
                }
                while (ret < 0 && errno == EINTR);

                if (ret < 0)
                {
                    return error_io("fail to read data from {}: {}", file_name, strerror(errno));
                }
                if (ret == 0)
                {
                    break; // EOF
                }

                total_read += static_cast<size_t>(ret);
                cur_off += static_cast<uint64_t>(ret);
            }
            return total_read;
        }
    } // namespace detail

    class PosixReadFile final : public ReadFile
    {
    public:
        std::string file_name_;
        FdGuard fd_guard_{};

        PosixReadFile(std::string file_name, FdGuard fd_guard) :
            file_name_(std::move(file_name)), fd_guard_(std::move(fd_guard))
        {
        }

        ResultStatus<size_t> Read(size_t read_size, char* read_buffer) override
        {
            // A zero-length request is a no-op; the buffer is allowed to be null then,
            // which is what an empty container hands out.
            if (read_size == 0)
                return 0;
            if (!read_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            return detail::ReadAll(fd_guard_.fd, file_name_, 0, false, read_size, read_buffer);
        }

        ResultStatus<size_t> RandomRead(uint64_t offset, size_t read_size, char* read_buffer) override
        {
            if (read_size == 0)
                return 0;
            if (!read_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            return detail::ReadAll(fd_guard_.fd, file_name_, offset, true, read_size, read_buffer);
        }

        ResultStatus<size_t> RandomReadV(const struct iovec* iov, int iov_cnt, uint64_t offset) override
        {
            if (iov == nullptr || iov_cnt <= 0)
                return error_run_time("invalid iov argument");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            std::vector<struct iovec> tmp_iov(iov, iov + iov_cnt);
            return detail::PreadvAll(fd_guard_.fd, file_name_, tmp_iov, offset);
        }

        ResultStatus<size_t> GetFileSize() override
        {
            struct stat st{};
            if (::fstat(fd_guard_.fd, &st) != 0)
            {
                return error_io("fstat {} failed: {}", file_name_, strerror(errno));
            }
            return static_cast<uint64_t>(st.st_size);
        }
    };

    class PosixWriteFile final : public WriteFile
    {
    public:
        std::string file_name_;
        FdGuard fd_guard_;

        PosixWriteFile(std::string file_name, FdGuard fd_guard) :
            file_name_(std::move(file_name)), fd_guard_(std::move(fd_guard))
        {
        }


        Status Write(const size_t write_size, const char* write_buffer) override
        {
            if (write_size == 0)
                return {};
            if (!write_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            const char* ptr = write_buffer;
            size_t remain = write_size;
            while (remain > 0)
            {
                const size_t want = std::min(remain, kMaxSingleIoBytes);
                ssize_t ret;
                do
                {
                    ret = ::write(fd_guard_.fd, ptr, want);
                }
                while (ret == -1 && errno == EINTR);

                if (ret == -1)
                {
                    return error_io("fail to seq write {}: {}", file_name_, strerror(errno));
                }
                if (ret == 0)
                {
                    return error_io("seq write made no progress on {}", file_name_);
                }
                ptr += ret;
                remain -= static_cast<size_t>(ret);
            }
            return {};
        }

        Status RandomWrite(uint64_t offset, size_t write_size, const char* write_buffer) override
        {
            if (write_size == 0)
                return {};
            if (!write_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            const char* ptr = write_buffer;
            size_t remain = write_size;
            uint64_t off = offset;
            while (remain > 0)
            {
                const size_t want = std::min(remain, kMaxSingleIoBytes);
                ssize_t ret;
                do
                {
                    ret = ::pwrite(fd_guard_.fd, ptr, want, static_cast<off_t>(off));
                }
                while (ret == -1 && errno == EINTR);

                if (ret == -1)
                {
                    return error_io("fail to rand write {}: {}", file_name_, strerror(errno));
                }
                if (ret == 0)
                {
                    return error_io("rand write made no progress on {}", file_name_);
                }
                ptr += ret;
                remain -= static_cast<size_t>(ret);
                off += static_cast<uint64_t>(ret);
            }
            return {};
        }

        Status RandomWriteV(const struct iovec* iov, int iov_cnt, uint64_t offset) override
        {
            if (iov == nullptr || iov_cnt <= 0)
                return error_run_time("invalid iov argument");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            std::vector<struct iovec> tmp_iov(iov, iov + iov_cnt);
            return detail::PwritevAll(fd_guard_.fd, file_name_, tmp_iov, offset);
        }

        Status Sync() override
        {
            if (!SyncFd(fd_guard_.fd))
            {
                return error_io("fail to sync {}: {}", file_name_, strerror(errno));
            }
            return {};
        }

        Status TruncFile(size_t target_file_size) override
        {
            if (::ftruncate(fd_guard_.fd, static_cast<off_t>(target_file_size)) < 0)
            {
                return error_io("fail to truncate {}: {}", file_name_, strerror(errno));
            }
            return {};
        }
    };

    ResultStatus<std::unique_ptr<ReadFile>> NewPosixReadFile(const std::string& file_name)
    {
        const int fd = ::open(file_name.c_str(), O_RDONLY | kOpenBaseFlags);
        if (fd < 0)
        {
            return error_io("fail to open {}: {}", file_name, strerror(errno));
        }
        FdGuard guard(fd);
        auto ptr = std::make_unique<PosixReadFile>(file_name, std::move(guard));
        return {std::move(ptr)};
    }

    ResultStatus<std::unique_ptr<WriteFile>> NewPosixWriteFile(const std::string& file_name, const bool truncate)
    {
        int open_flags = O_WRONLY | O_CREAT | kOpenBaseFlags;
        if (truncate)
        {
            open_flags |= O_TRUNC;
        }
        const int fd = ::open(file_name.c_str(), open_flags, 0644);
        if (fd < 0)
        {
            return error_io("fail to create or open file {}: {}", file_name, strerror(errno));
        }
        FdGuard guard(fd);
        auto ptr = std::make_unique<PosixWriteFile>(file_name, std::move(guard));
        return {std::move(ptr)};
    }

} // namespace ctar
