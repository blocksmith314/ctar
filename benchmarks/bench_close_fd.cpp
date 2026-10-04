#include <benchmark/benchmark.h>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

constexpr int FILE_COUNT = 100;
constexpr int BUF_SIZE = 4096;
const std::string BENCH_BASE_DIR = "./tmp_fd_bench";
const std::string DIR_PER_FILE_CLOSE = BENCH_BASE_DIR + "/per_file_close";
const std::string DIR_BATCH_LATE_CLOSE = BENCH_BASE_DIR + "/batch_late_close";
const std::string DIR_CLOSE_RANGE = BENCH_BASE_DIR + "/close_range";

static void PrepareTestFiles(const std::string& target_dir)
{
    fs::create_directories(target_dir);
    unsigned char buf[BUF_SIZE];
    std::fill_n(buf, BUF_SIZE, 0xABU);

    for (int i = 0; i < FILE_COUNT; ++i)
    {
        std::string path = target_dir + "/" + std::to_string(i) + ".dat";
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            throw std::runtime_error("create test file failed: " + path);
        }
        ssize_t written = write(fd, buf, BUF_SIZE);
        if (written != BUF_SIZE || close(fd) != 0)
        {
            throw std::runtime_error("write or close test file failed: " + path);
        }
    }
}

static void CleanTestFiles()
{
    fs::remove_all(BENCH_BASE_DIR);
}

static void BM_PerFileClose(benchmark::State& state)
{
    char buf[BUF_SIZE];
    std::vector<std::string> paths;
    paths.reserve(FILE_COUNT);
    for (int i = 0; i < FILE_COUNT; ++i)
    {
        paths.push_back(DIR_PER_FILE_CLOSE + "/" + std::to_string(i) + ".dat");
    }

    for (auto _ : state)
    {
        for (const auto& p : paths)
        {
            int fd = open(p.c_str(), O_RDONLY);
            if (fd < 0)
            {
                throw std::runtime_error("open test file failed: " + p);
            }
            if (read(fd, buf, BUF_SIZE) != BUF_SIZE || close(fd) != 0)
            {
                throw std::runtime_error("read or close test file failed: " + p);
            }
        }
    }
}

static void BM_BatchClose(benchmark::State& state)
{
    char buf[BUF_SIZE];
    std::vector<std::string> paths;
    std::vector<int> fds;
    paths.reserve(FILE_COUNT);
    fds.reserve(FILE_COUNT);

    for (int i = 0; i < FILE_COUNT; ++i)
    {
        paths.emplace_back(DIR_BATCH_LATE_CLOSE + "/" + std::to_string(i) + ".dat");
    }

    for (auto _ : state)
    {
        fds.clear();
        for (const auto& p : paths)
        {
            int fd = open(p.c_str(), O_RDONLY);
            if (fd < 0)
            {
                throw std::runtime_error("open test file failed: " + p);
            }
            if (read(fd, buf, BUF_SIZE) != BUF_SIZE)
            {
                throw std::runtime_error("read test file failed");
            }
            fds.push_back(fd);
        }

        for (int fd : fds)
        {
            if (close(fd) != 0)
            {
                throw std::runtime_error("close test file failed");
            }
        }
    }
}
#if defined(linux)
static void BM_CloseRange (benchmark::State& state)
{
    char buf [BUF_SIZE];
    std::vector<std::string> paths;
    std::vector<int> fds;
    paths.reserve(FILE_COUNT);
    fds.reserve(FILE_COUNT);

    for (int i = 0; i < FILE_COUNT; ++i)
    {
        paths.emplace_back(DIR_CLOSE_RANGE + "/" + std::to_string(i) + ".dat");
    }

    for (auto _ : state)
    {
        fds.clear();
        for (const auto& p : paths)
        {
            int fd = open(p.c_str(), O_RDONLY);
            if (fd < 0)
            {
                throw std::runtime_error("open test file failed: " + p);
            }
            if (read(fd, buf, BUF_SIZE) != BUF_SIZE)
            {
                throw std::runtime_error("read test file failed");
            }
            fds.push_back(fd);
        }

        if (syscall(SYS_close_range, static_cast(fds.front()), static_cast(fds.back()),
        0) != 0)
        {
            throw std::runtime_error("close_range failed: " + std::to_string(errno));
        }
    }
}
#endif

BENCHMARK(BM_PerFileClose);
BENCHMARK(BM_BatchClose);

#if defined(linux)
BENCHMARK(BM_CloseRange);
#endif
int main(int argc, char** argv)
{
    PrepareTestFiles(std::string(DIR_PER_FILE_CLOSE));
    PrepareTestFiles(std::string(DIR_BATCH_LATE_CLOSE));
    PrepareTestFiles(std::string(DIR_CLOSE_RANGE));
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    CleanTestFiles();
    return 0;
} //   benchmarks/close_fd.cpp

