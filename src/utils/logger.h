#ifndef CTAR_LOGGER_H
#define CTAR_LOGGER_H

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <print>
#include <source_location>
#include <string_view>
#include <tuple>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace ctar
{
    enum class Level
    {
        DEBUG,
        INFO,
        WARN,
        ERR
    };


    class Logger
    {
    public:
        static Logger& instance()
        {
            static Logger logger;
            return logger;
        }

        template <typename... Args>
        void log(Level level, const std::source_location& loc, std::string_view fmt, Args&&... args)
        {
            std::lock_guard lock(mtx_);

            auto now = std::chrono::system_clock::now();
            auto pid = get_pid();
            auto tid = get_native_tid();

            std::string_view full_path = loc.file_name();

            std::string user_msg = std::vformat(fmt, std::make_format_args(args...));

            std::print(std::cout, "[{:%Y-%m-%d %H:%M:%S}] [{}:{}] [{}] [{}:{}] {}\n", now, pid, tid,
                       level_to_string(level), full_path, loc.line(), user_msg);
        }


    private:
        std::mutex mtx_;
        Logger() = default;

        static uint32_t get_pid()
        {
#ifdef _WIN32
            return static_cast<uint32_t>(_getpid());
#else
            return static_cast<uint32_t>(getpid());
#endif
        }

        static uint64_t get_native_tid()
        {
#if defined(_WIN32)
            return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__linux__)
            return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
            uint64_t tid;
            pthread_threadid_np(nullptr, &tid);
            return tid;
#else
            static std::atomic<uint32_t> counter{0};
            static thread_local uint32_t local_id = ++counter;
            return static_cast<uint64_t>(local_id);
#endif
        }

        static std::string_view level_to_string(Level l)
        {
            switch (l)
            {
            case Level::INFO:
                return "\033[32mINFO\033[0m";
            case Level::WARN:
                return "\033[33mWARN\033[0m";
            case Level::ERR:
                return "\033[31mERROR\033[0m";
            case Level::DEBUG:
                return "\033[36mDEBUG\033[0m";
            default:
                return "LOG";
            }
        }
    };


#define LOG_INFO(...) ctar::Logger::instance().log(ctar::Level::INFO, std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(...) ctar::Logger::instance().log(ctar::Level::WARN, std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(...) ctar::Logger::instance().log(ctar::Level::ERR, std::source_location::current(), __VA_ARGS__)
#define LOG_DEBUG(...) ctar::Logger::instance().log(ctar::Level::DEBUG, std::source_location::current(), __VA_ARGS__)



} // namespace ctar

#endif // CTAR_LOGGER_H