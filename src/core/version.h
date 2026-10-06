#ifndef CTAR_VERSION_H
#define CTAR_VERSION_H

#include <cstdint>
#include <format>
#include <string>

namespace ctar
{

    // Tool version -- the single source of truth for the release number.
    // cmake/version.cmake extracts these three scalars textually and feeds them
    // into project(VERSION ...), so the CLI, ctar.pc and ctarConfigVersion.cmake
    // all agree.
    constexpr uint8_t kToolVersionMajor = 1;
    constexpr uint8_t kToolVersionMinor = 0;
    constexpr uint8_t kToolVersionPatch = 0;

    // On-disk archive format version, stamped into every archive header.
    // Bump this ONLY when the byte layout changes -- never to match the tool
    // version above.
    constexpr uint8_t kFileFormatVersionMajor = 1;
    constexpr uint8_t kFileFormatVersionMinor = 0;
    constexpr uint8_t kFileFormatVersionPatch = 0;

    struct Version
    {
        uint8_t major{};
        uint8_t minor{};
        uint8_t patch{};
        uint8_t reserved{};

        constexpr Version() noexcept = default;

        constexpr Version(uint8_t maj, uint8_t min, uint8_t pat) noexcept : major(maj), minor(min), patch(pat) {}

        [[nodiscard]] constexpr uint32_t Encode() const noexcept
        {
            return (static_cast<uint32_t>(reserved) << 24U) | (static_cast<uint32_t>(patch) << 16U) |
                (static_cast<uint32_t>(minor) << 8U) | static_cast<uint32_t>(major);
        }

        [[nodiscard]] static constexpr Version Decode(const uint32_t ver) noexcept
        {
            Version v;
            v.reserved = static_cast<uint8_t>((ver >> 24U) & 0xFFU);
            v.patch = static_cast<uint8_t>((ver >> 16U) & 0xFFU);
            v.minor = static_cast<uint8_t>((ver >> 8U) & 0xFFU);
            v.major = static_cast<uint8_t>(ver & 0xFFU);
            return v;
        }

        [[nodiscard]] std::string to_string() const { return std::format("{}.{}.{}", major, minor, patch); }
    };

    // Compile-time conveniences derived from the scalars above. The scalars stay
    // the authority because they are what CMake parses.
    inline constexpr Version kToolVersion{kToolVersionMajor, kToolVersionMinor, kToolVersionPatch};

    inline constexpr Version kFileFormatVersion{kFileFormatVersionMajor, kFileFormatVersionMinor,
                                                kFileFormatVersionPatch};

} // namespace ctar

#endif // CTAR_VERSION_H