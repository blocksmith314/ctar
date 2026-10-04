#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <cerrno>

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
            if (!read_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            ssize_t bytes_read;
            do
            {
                bytes_read = ::read(fd_guard_.fd, read_buffer, read_size);
            }
            while (bytes_read < 0 && errno == EINTR);

            if (bytes_read < 0)
            {
                return error_io("fail to read data from {}: {}", file_name_, strerror(errno));
            }
            return static_cast<size_t>(bytes_read);
        }

        ResultStatus<size_t> RandomRead(uint64_t offset, size_t read_size, char* read_buffer) override
        {
            if (!read_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            ssize_t bytes_read;
            do
            {
                bytes_read = ::pread(fd_guard_.fd, read_buffer, read_size, static_cast<off_t>(offset));
            }
            while (bytes_read < 0 && errno == EINTR);

            if (bytes_read < 0)
            {
                return error_io("fail to read data from {}: {}", file_name_, strerror(errno));
            }
            return static_cast<size_t>(bytes_read);
        }

        ResultStatus<size_t> RandomReadV(const struct iovec* iov, int iov_cnt, uint64_t offset) override
        {
            if (iov == nullptr || iov_cnt <= 0)
                return error_run_time("invalid iov argument");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            std::vector<struct iovec> tmp_iov(iov, iov + iov_cnt);
            off_t cur_off = static_cast<off_t>(offset);
            size_t total_read = 0;

            while (!tmp_iov.empty())
            {
                ssize_t ret;
                do
                {
                    ret = preadv(fd_guard_.fd, tmp_iov.data(), static_cast<int>(tmp_iov.size()), cur_off);
                }
                while (ret < 0 && errno == EINTR);
                if (ret < 0)
                {
                    return error_io("fail to preadv {}: {}", file_name_, strerror(errno));
                }
                if (ret == 0)
                {
                    break; // EOF
                }

                total_read += static_cast<size_t>(ret);
                cur_off += ret;
                size_t remain = static_cast<size_t>(ret);

                for (auto& iv : tmp_iov)
                {
                    if (remain == 0)
                        break;
                    if (iv.iov_len <= remain)
                    {
                        remain -= iv.iov_len;
                        iv.iov_base = nullptr;
                        iv.iov_len = 0;
                    }
                    else
                    {
                        iv.iov_base = static_cast<char*>(iv.iov_base) + remain;
                        iv.iov_len -= remain;
                        remain = 0;
                    }
                }

                std::erase_if(tmp_iov, [](const struct iovec& iv) { return iv.iov_len == 0; });
            }
            return total_read;
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
            if (!write_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            const char* ptr = write_buffer;
            size_t remain = write_size;
            while (remain > 0)
            {
                ssize_t ret;
                do
                {
                    ret = ::write(fd_guard_.fd, ptr, remain);
                }
                while (ret == -1 && errno == EINTR);

                if (ret == -1)
                {
                    return error_io("fail to seq write {}: {}", file_name_, strerror(errno));
                }
                ptr += ret;
                remain -= static_cast<size_t>(ret);
            }
            return {};
        }

        Status RandomWrite(uint64_t offset, size_t write_size, const char* write_buffer) override
        {
            if (!write_buffer)
                return error_run_time("null buffer");
            if (fd_guard_.fd < 0)
                return error_run_time("file not open");

            const char* ptr = write_buffer;
            size_t remain = write_size;
            uint64_t off = offset;
            while (remain > 0)
            {
                ssize_t ret;
                do
                {
                    ret = ::pwrite(fd_guard_.fd, ptr, remain, static_cast<off_t>(off));
                }
                while (ret == -1 && errno == EINTR);

                if (ret == -1)
                {
                    return error_io("fail to rand write {}: {}", file_name_, strerror(errno));
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
            off_t cur_off = static_cast<off_t>(offset);

            while (!tmp_iov.empty())
            {
                ssize_t ret;
                do
                {
                    ret = pwritev(fd_guard_.fd, tmp_iov.data(), static_cast<int>(tmp_iov.size()), cur_off);
                }
                while (ret < 0 && errno == EINTR);

                if (ret < 0)
                {
                    return error_io("fail to pwritev {}: {}", file_name_, strerror(errno));
                }
                if (ret == 0)
                {
                    return error_io("pwritev zero write, unexpected");
                }

                cur_off += ret;
                size_t remain = static_cast<size_t>(ret);

                for (auto& iv : tmp_iov)
                {
                    if (remain == 0)
                        break;
                    if (iv.iov_len <= remain)
                    {
                        remain -= iv.iov_len;
                        iv.iov_base = nullptr;
                        iv.iov_len = 0;
                    }
                    else
                    {
                        iv.iov_base = static_cast<char*>(iv.iov_base) + remain;
                        iv.iov_len -= remain;
                        remain = 0;
                    }
                }

                std::erase_if(tmp_iov, [](const struct iovec& iv) { return iv.iov_len == 0; });
            }
            return {};
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
