#ifndef CTAR_FILE_HANDLE_H
#define CTAR_FILE_HANDLE_H

#include <memory>
#include <string>
#include <cstdint>
#include <vector>

#include "src/utils/status.h"

namespace ctar
{

#if defined(__linux__) || defined(__APPLE__)
#include <sys/uio.h>
#else
    struct iovec
    {
        void* iov_base;
        size_t iov_len;
    };
#endif

    class ReadFile
    {
    public:
        ReadFile() = default;
        virtual ~ReadFile() noexcept = default;

        ReadFile(const ReadFile&) = delete;
        ReadFile& operator=(const ReadFile&) = delete;
        ReadFile(ReadFile&&) noexcept = default;
        ReadFile& operator=(ReadFile&&) noexcept = default;

        virtual ResultStatus<size_t> Read(size_t read_size, char* read_buffer) = 0;
        virtual ResultStatus<size_t> RandomRead(uint64_t offset, size_t read_size, char* read_buffer) = 0;
        virtual ResultStatus<size_t> RandomReadV(const struct iovec* iov, int iov_cnt, uint64_t offset) = 0;
        virtual ResultStatus<size_t> GetFileSize() = 0;
    };

    class WriteFile
    {
    public:
        WriteFile() = default;
        virtual ~WriteFile() noexcept = default;

        WriteFile(const WriteFile&) = delete;
        WriteFile& operator=(const WriteFile&) = delete;
        WriteFile(WriteFile&&) noexcept = default;
        WriteFile& operator=(WriteFile&&) noexcept = default;

        virtual Status Write(size_t write_size, const char* write_buffer) = 0;
        virtual Status RandomWrite(uint64_t offset, size_t write_size, const char* write_buffer) = 0;
        virtual Status RandomWriteV(const struct iovec* iov, int iov_cnt, uint64_t offset) = 0;
        virtual Status Sync() = 0;
        virtual Status TruncFile(size_t target_file_size) = 0;
    };

    ResultStatus<std::unique_ptr<ReadFile>> NewPosixReadFile(const std::string& file_name);
    ResultStatus<std::unique_ptr<WriteFile>> NewPosixWriteFile(const std::string& file_name, bool truncate = false);

    // Bounds for a single read/write syscall. The kernel limits how much one call may
    // carry, and the two platforms react differently when the request is too large:
    //   Linux clamps the count to MAX_RW_COUNT (0x7ffff000) and returns a short count.
    //   macOS rejects anything above INT_MAX with EINVAL and transfers nothing
    //   (measured: 2147483647 completes, 2147483648 fails).
    // 1 GiB stays below both limits; the extra syscalls are negligible next to the I/O.
    inline constexpr size_t kMaxSingleIoBytes = 1ULL << 30;

    // IOV_MAX is 1024 on Linux and macOS; preadv/pwritev reject more entries with EINVAL.
    inline constexpr int kMaxIovPerCall = 1024;

    namespace detail
    {
        /// Write every byte described by @p iovs at @p offset, splitting the request into
        /// syscalls of at most @p max_iov entries and @p max_bytes bytes. The limits are
        /// parameters so tests can drive the splitting loop with small buffers instead of
        /// allocating gigabytes. A zero-length total is a successful no-op, as for writev().
        Status PwritevAll(int fd, const std::string& file_name, std::vector<struct iovec>& iovs, uint64_t offset,
                          size_t max_bytes = kMaxSingleIoBytes, int max_iov = kMaxIovPerCall);

        /// Read counterpart of PwritevAll. The returned size is smaller than the requested
        /// total only when EOF is reached.
        ResultStatus<size_t> PreadvAll(int fd, const std::string& file_name, std::vector<struct iovec>& iovs,
                                       uint64_t offset, size_t max_bytes = kMaxSingleIoBytes,
                                       int max_iov = kMaxIovPerCall);

        /// Read up to @p read_size bytes into @p read_buffer, stopping early only at EOF.
        /// When @p positioned is true the read starts at @p offset and leaves the file
        /// offset untouched; otherwise the file offset advances as usual.
        ResultStatus<size_t> ReadAll(int fd, const std::string& file_name, uint64_t offset, bool positioned,
                                     size_t read_size, char* read_buffer, size_t max_bytes = kMaxSingleIoBytes);
    } // namespace detail


#if defined(HAVE_O_CLOEXEC)
    constexpr int kOpenBaseFlags = O_CLOEXEC;
#else
    constexpr int kOpenBaseFlags = 0;
#endif

} // namespace ctar
#endif
