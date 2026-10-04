#include <fcntl.h>
#include <filesystem>
#include <unistd.h>
#include "benchmark/benchmark.h"
#include "src/core/block_store.h"
#include "test/tools/file_generator.h"

namespace fs = std::filesystem;

namespace
{
    constexpr size_t kFirstLevelDirs = 10;
    constexpr size_t kMaxDirDepth = 5;
    constexpr size_t kMinFileSize = 128ULL;
    constexpr size_t kMaxFileSize = 2048000ULL;
    constexpr bool kCreateJunk = false;

    void CleanupDirs(const std::initializer_list<fs::path> dirs)
    {
        for (const auto& d : dirs)
        {
            std::error_code ec;
            fs::remove_all(d, ec);
        }
    }
} // namespace

static void BM_Pack(benchmark::State& state)
{
    const size_t file_count = state.range(0);
    const fs::path source_dir{"bm_pack_cold_src"};
    const fs::path pack_dir{"bm_pack_cold_out"};
    const std::string pack_file = (pack_dir / "test.ctar").string();

    CleanupDirs({source_dir, pack_dir});
    fs::create_directories(source_dir);
    fs::create_directories(pack_dir);

    uint64_t total_bytes = 0;
    uint64_t total_files = 0;

    for (auto _ : state)
    {
        state.PauseTiming();
        CleanupDirs({source_dir});
        fs::create_directories(source_dir);
        if (fs::exists(pack_file))
            fs::remove(pack_file);

        ctar::test::FileGenerator gen(source_dir);
        auto st = gen.GenerateRandomFiles(kFirstLevelDirs, kMaxDirDepth, file_count, kMinFileSize, kMaxFileSize,
                                          kCreateJunk, ctar::test::CompressLevel::MediumCompress);
        if (!st)
        {
            state.SkipWithError("Failed to generate test files");
            break;
        }
        total_bytes = gen.total_file_size;
        total_files = gen.file_cnt;
        state.ResumeTiming();

        ctar::BlockStore blob_store;
        auto s = blob_store.Scan(source_dir);
        if (!s)
        {
            state.SkipWithError("Scan failed");
            break;
        }
        s = blob_store.PackPipeline(pack_file);
        if (!s)
        {
            state.SkipWithError("Pack failed");
            break;
        }
        benchmark::DoNotOptimize(s);
    }

    const double mb = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    state.counters["Throughput(MB/s)"] = benchmark::Counter(mb, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["Files/s"] =
        benchmark::Counter(static_cast<double>(total_files), benchmark::Counter::kIsIterationInvariantRate);

    CleanupDirs({source_dir, pack_dir});
}

BENCHMARK(BM_Pack)->Unit(benchmark::kMillisecond)->Arg(100)->Arg(1000)->Arg(5000)->Arg(10000)->UseRealTime();

static void BM_Unpack(benchmark::State& state)
{
    const size_t file_count = state.range(0);
    const fs::path source_dir{"bm_unpack_cold_src"};
    const fs::path pack_dir{"bm_unpack_cold_pack"};
    const fs::path unpack_dir{"bm_unpack_cold_out"};
    const std::string pack_file = (pack_dir / "test.ctar").string();

    CleanupDirs({source_dir, pack_dir, unpack_dir});
    fs::create_directories(source_dir);
    fs::create_directories(pack_dir);

    uint64_t total_bytes = 0;
    uint64_t total_files = 0;

    for (auto _ : state)
    {
        state.PauseTiming();
        CleanupDirs({source_dir, unpack_dir});
        fs::create_directories(source_dir);
        fs::create_directories(unpack_dir);

        ctar::test::FileGenerator gen(source_dir);
        auto st = gen.GenerateRandomFiles(kFirstLevelDirs, kMaxDirDepth, file_count, kMinFileSize, kMaxFileSize,
                                          kCreateJunk, ctar::test::CompressLevel::MediumCompress);
        if (!st)
        {
            state.SkipWithError("Failed to generate test files");
            break;
        }
        total_bytes = gen.total_file_size;
        total_files = gen.file_cnt;

        {
            ctar::BlockStore blob;
            auto status = blob.Scan(source_dir);
            if (!status)
            {
                state.SkipWithError("Pre-scan failed");
                break;
            }
            status = blob.PackPipeline(pack_file);
            if (!status)
            {
                state.SkipWithError("Pre-ctar failed");
                break;
            }
        }
        state.ResumeTiming();

        ctar::BlockStore blob_store;
        auto s = blob_store.UnPackPipeline(pack_file, unpack_dir.string());
        if (!s)
        {
            state.SkipWithError("UnPack failed");
            break;
        }
        benchmark::DoNotOptimize(s);
    }

    const double mb = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    state.counters["Throughput(MB/s)"] = benchmark::Counter(mb, benchmark::Counter::kIsIterationInvariantRate);
    state.counters["Files/s"] =
        benchmark::Counter(static_cast<double>(total_files), benchmark::Counter::kIsIterationInvariantRate);

    CleanupDirs({source_dir, pack_dir, unpack_dir});
}

BENCHMARK(BM_Unpack)->Unit(benchmark::kMillisecond)->Arg(100)->Arg(1000)->Arg(5000)->Arg(10000)->UseRealTime();

BENCHMARK_MAIN();
