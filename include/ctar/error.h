#ifndef CTAR_ERROR_H
#define CTAR_ERROR_H

#include <cstdint>
#include <expected>
#include <string>

namespace ctar
{

    /// Stable error codes exposed by the public API. Internal error types are
    /// mapped onto these values and never leak through the installed headers.
    enum class ErrorCode : uint8_t
    {
        kNotFound = 0,
        kInvalidArgument,
        kCorruption,
        kIo,
        kTooLarge,
        kUnsupported,
        kRuntimeError,
    };

    struct Error
    {
        ErrorCode code = ErrorCode::kRuntimeError;
        std::string message;
    };

    template <typename T>
    using Result = std::expected<T, Error>;

} // namespace ctar

#endif // CTAR_ERROR_H
