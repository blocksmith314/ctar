#ifndef CTAR_STATUS_H
#define CTAR_STATUS_H

#include <expected>
#include <format>
#include <string>

namespace ctar
{
    enum class ErrorType
    {
        kSuccess = 0,
        kNotFound = 1,
        kCorruption = 2,
        kNotSupported = 3,
        kInvalidArgument = 4,
        kIoError = 5,
        kRuntimeError = 6
    };

    struct ErrorStatus
    {
        ErrorType err_type;
        std::string err_message;
        std::string source_file;
        int source_line = 0;

        ErrorStatus() : err_type(ErrorType::kSuccess) {}

        ErrorStatus(const ErrorType t, std::string msg, std::string file = "", const int line = 0) :
            err_type(t), err_message(std::move(msg)), source_file(std::move(file)), source_line(line)
        {
        }

        [[nodiscard]] bool ok() const noexcept { return err_type == ErrorType::kSuccess; }

        [[nodiscard]] ErrorType type() const noexcept { return err_type; }

        [[nodiscard]] std::string message() const
        {
            return std::format("{}:{} {}", source_file, source_line, err_message);
        }

        explicit operator std::string() const { return message(); }
    };

    inline std::unexpected<ErrorStatus> make_error(ErrorType type, std::string msg, std::string src_file, int src_line)
    {
        return std::unexpected<ErrorStatus>(std::in_place, type, std::move(msg), std::move(src_file), src_line);
    }

#define error_io(...) make_error(ErrorType::kIoError, std::format(__VA_ARGS__), __FILE__, __LINE__)

#define error_not_found(...) make_error(ErrorType::kNotFound, std::format(__VA_ARGS__), __FILE__, __LINE__)

#define error_run_time(...) make_error(ErrorType::kRuntimeError, std::format(__VA_ARGS__), __FILE__, __LINE__)

#define error_corruption(...) make_error(ErrorType::kCorruption, std::format(__VA_ARGS__), __FILE__, __LINE__)

#define error_not_supported(...) make_error(ErrorType::kNotSupported, std::format(__VA_ARGS__), __FILE__, __LINE__)

#define error_invalid_arg(...) make_error(ErrorType::kInvalidArgument, std::format(__VA_ARGS__), __FILE__, __LINE__)

    using Status = std::expected<void, ErrorStatus>;
    template <typename T>
    using ResultStatus = std::expected<T, ErrorStatus>;
} // namespace ctar

#endif // CTAR_STATUS_H
