#ifndef CTAR_SERIALIZE_HELPER_H
#define CTAR_SERIALIZE_HELPER_H
#include <filesystem>
#include <unordered_set>
namespace ctar
{

    namespace fs = std::filesystem;
    inline constexpr uint64_t next_4k_align(uint64_t n)
    {
        constexpr uint64_t BLOCK = 4096;
        uint64_t q = n / BLOCK;
        uint64_t r = n % BLOCK;
        return (r == 0) ? q * BLOCK : (q + 1) * BLOCK;
    }

    static const std::unordered_set<std::string> junk_files = {".DS_Store", "._.DS_Store"};

    constexpr size_t FIX64_LEN = sizeof(uint64_t);

    inline void skip_ptr(const char*& p, size_t byte) noexcept { p += byte; }

    constexpr size_t kDefaultBufferSize = 4096;


    template <typename T>
    static constexpr size_t FixedFieldSize(const T&) noexcept
    {
        return sizeof(std::decay_t<T>);
    }

    template <typename... Fields>
    static constexpr size_t FixedFieldTotalSize(const Fields&... fields) noexcept
    {
        return (FixedFieldSize(fields) + ...);
    }


    template <typename C>
    concept SerializableContainer = requires(const C& obj) {
        obj.size();
        obj.data();
        typename C::value_type;
    };

    template <SerializableContainer C>
    static constexpr size_t ContainerSerializedSize(const C& c) noexcept
    {
        using Elem = typename C::value_type;
        return sizeof(uint64_t) + c.size() * sizeof(Elem);
    }

    template <SerializableContainer... Cs>
    static constexpr size_t ContainerSerializedTotalSize(const Cs&... containers) noexcept
    {
        return (ContainerSerializedSize(containers) + ...);
    }

    [[nodiscard]] inline bool is_skip_entry(const fs::directory_entry& entry) noexcept
    {
        return junk_files.contains(entry.path().filename());
    }

    /// @brief Convert unix permission mode bits to 9‑character rwx string
    /// @param mode Unix permission value, only lower 9 bits are used
    /// @return 9‑char string like "rw‑r--r--", no leading file type indicator ('d'/'-')
    inline std::string PermBitsToString(uint32_t mode)
    {
        char buf[10];
        buf[0] = (mode & 0400) ? 'r' : '-';
        buf[1] = (mode & 0200) ? 'w' : '-';
        buf[2] = (mode & 0100) ? 'x' : '-';
        buf[3] = (mode & 0040) ? 'r' : '-';
        buf[4] = (mode & 0020) ? 'w' : '-';
        buf[5] = (mode & 0010) ? 'x' : '-';
        buf[6] = (mode & 0004) ? 'r' : '-';
        buf[7] = (mode & 0002) ? 'w' : '-';
        buf[8] = (mode & 0001) ? 'x' : '-';
        buf[9] = '\0';
        return {buf};
    }

    /// @brief Format unix seconds timestamp to human‑readable time string
    /// If timestamp within half‑year from now: "Sep 27 15:42"
    /// If older than half‑year:             "Sep 27  2025"
    /// Input timestamp is unix epoch in seconds, NOT milliseconds.
    /// @param timestamp Unix epoch time in seconds
    /// @return Formatted local time string
    inline std::string FormatTime(long long timestamp)
    {
        constexpr long long half_year_sec = 183LL * 24 * 3600;
        const auto now = std::time(nullptr);
        const auto mtime = static_cast<std::time_t>(timestamp);
        char buf[20];
        // NOTE: std::localtime is not thread-safe
        if (now - mtime < half_year_sec)
        {
            std::strftime(buf, sizeof(buf), "%b %d %H:%M", std::localtime(&mtime));
        }
        else
        {
            std::strftime(buf, sizeof(buf), "%b %d  %Y", std::localtime(&mtime));
        }
        return {buf};
    }

} // namespace ctar

#endif // CTAR_SERIALIZE_HELPER_H
