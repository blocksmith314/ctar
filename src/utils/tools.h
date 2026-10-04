#ifndef CTAR_TOOLS_H
#define CTAR_TOOLS_H

#include <print>

namespace ctar
{

    class ScopeTimer
    {
    public:
        using Clock = std::chrono::high_resolution_clock;

        explicit ScopeTimer(std::string tag = "", bool output = true) :
            m_tag_(std::move(tag)), output_(output), m_begin_(Clock::now())
        {
        }
        ScopeTimer(const ScopeTimer&) = delete;
        ScopeTimer& operator=(const ScopeTimer&) = delete;

        ScopeTimer(ScopeTimer&&) noexcept = default;
        ScopeTimer& operator=(ScopeTimer&&) noexcept = default;

        template <typename Duration = std::chrono::milliseconds>
        [[nodiscard]] long long elapsed() const
        {
            return std::chrono::duration_cast<Duration>(Clock::now() - m_begin_).count();
        }
        ~ScopeTimer()
        {
            if (output_)
            {
                auto ms = elapsed<std::chrono::milliseconds>();
                if (m_tag_.empty())
                {
                    std::println("scope cost: {}",ms);
                }
                else
                {
                    std::println("[{}] cost: {}",m_tag_,ms);
                }
            }
        }

    private:
        std::string m_tag_;
        bool output_;
        Clock::time_point m_begin_;
    };

    /// @brief Calculate compression ratio (compressed / original)
    /// @param original_size original uncompressed byte size
    /// @param compressed_size compressed byte size
    /// @return compression ratio; returns 1.0 if original_size is zero
    inline double CompressionRatio(size_t original_size, size_t compressed_size) noexcept
    {
        if (original_size == 0)
        {
            return 1.0;
        }
        return static_cast<double>(compressed_size) * 100 / static_cast<double>(original_size);
    }

    inline std::string HumanReadableBytes(uint64_t bytes)
    {
        constexpr const char* units[] = {"B", "K", "M", "G", "T"};
        constexpr uint64_t base = 1024;
        size_t unit_index = 0;
        auto size = static_cast<double>(bytes);
        while (size >= base && unit_index < std::size(units) - 1)
        {
            size /= base;
            ++unit_index;
        }
        char buf[32];
        if (unit_index == 0)
        {
            std::snprintf(buf, sizeof(buf), "%llu%s", static_cast<unsigned long long>(bytes), units[unit_index]);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%.1f%s", size, units[unit_index]);
        }
        return {buf};
    }

    /// @brief Calculate throughput per second
    /// @param total_bytes total processed bytes
    /// @param elapsed_ms elapsed time
    /// @return throughput bytes per second; returns 0.0 if duration is zero
    inline std::string ThroughputPerSec(uint64_t total_bytes, long long elapsed_ms)
    {
        using namespace std::chrono;
        if (elapsed_ms <= 0)
        {
            return "0.0B";
        }
        const double sec = static_cast<double>(elapsed_ms) / 1'000;
        const auto throughput = static_cast<uint64_t>(static_cast<double>(total_bytes) / sec);
        return HumanReadableBytes(throughput);
    }

} // namespace ctar

#endif // CTAR_TOOLS_H
