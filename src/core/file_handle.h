#ifndef CTAR_FILE_HANDLE_H
#define CTAR_FILE_HANDLE_H

#include <memory>
#include <string>
#include <cstdint>

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



#if defined(HAVE_O_CLOEXEC)
    constexpr int kOpenBaseFlags = O_CLOEXEC;
#else
    constexpr int kOpenBaseFlags = 0;
#endif

} // namespace ctar
#endif
