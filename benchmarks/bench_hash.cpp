#include <string>
#include "benchmark/benchmark.h"
#include "xxhash.h"

namespace pack
{

#if defined(__x86_64__) && defined(__SSE4_2__)
#include <nmmintrin.h>
    uint32_t HardwareExtend(uint32_t crc, const uint8_t* data, size_t n)
    {
        uint64_t current = crc;
        size_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            uint64_t val;
            std::memcpy(&val, data + i, sizeof(val));
            current = _mm_crc32_u64(current, val);
        }
        for (; i < n; ++i)
        {
            current = _mm_crc32_u8(static_cast<uint32_t>(current), data[i]);
        }
        return static_cast<uint32_t>(current);
    }
#elif defined(__aarch64__)
#include <arm_acle.h>
    uint32_t HardwareExtend(uint32_t crc, const uint8_t* data, size_t n)
    {
        size_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            uint64_t val;
            std::memcpy(&val, data + i, sizeof(val));
            crc = __crc32cd(crc, val);
        }
        for (; i < n; ++i)
        {
            crc = __crc32cb(crc, data[i]);
        }
        return crc;
    }
#else
    uint32_t HardwareExtend(uint32_t /*crc*/, const uint8_t* /*data*/, size_t /*n*/) { return 0; }
#endif

    static std::string GenerateTestData(size_t size)
    {
        std::string buf(size, '\0');
        for (size_t i = 0; i < size; i++)
        {
            buf[i] = static_cast<char>((i * 41 + 13) & 0xFFU);
        }
        return buf;
    }

    const auto data_64 = GenerateTestData(64);
    const auto data_1k = GenerateTestData(1024);
    const auto data_4k = GenerateTestData(4 * 1024);
    const auto data_64k = GenerateTestData(64 * 1024);
    const auto data_1m = GenerateTestData(1024 * 1024);


    // ========== XXH32 ==========
    static void BM_XXH32_64(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_64.data());
            uint32_t h = XXH32(buf, data_64.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_64.size());
    }

    static void BM_XXH32_1K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_1k.data());
            uint32_t h = XXH32(buf, data_1k.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_1k.size());
    }

    static void BM_XXH32_4K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_4k.data());
            uint32_t h = XXH32(buf, data_4k.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_4k.size());
    }

    static void BM_XXH32_64K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_64k.data());
            uint32_t h = XXH32(buf, data_64k.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_64k.size());
    }

    static void BM_XXH32_1M(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_1m.data());
            uint32_t h = XXH32(buf, data_1m.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_1m.size());
    }

    // ========== XXH64 ==========
    static void BM_XXH64_64(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_64.data());
            uint64_t h = XXH64(buf, data_64.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_64.size());
    }

    static void BM_XXH64_1K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_1k.data());
            uint64_t h = XXH64(buf, data_1k.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_1k.size());
    }

    static void BM_XXH64_4K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_4k.data());
            uint64_t h = XXH64(buf, data_4k.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_4k.size());
    }

    static void BM_XXH64_64K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_64k.data());
            uint64_t h = XXH64(buf, data_64k.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_64k.size());
    }

    static void BM_XXH64_1M(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_1m.data());
            uint64_t h = XXH64(buf, data_1m.size(), 0);
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_1m.size());
    }

// ================= Hardware CRC32C (ARM64 / x86_64) =================
#if (defined(__x86_64__) && defined(__SSE4_2__)) || defined(__aarch64__)
    static void BM_CRC32C_HW_64(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_64.data());
            uint32_t h = HardwareExtend(0, buf, data_64.size());
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_64.size());
    }

    static void BM_CRC32C_HW_1K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_1k.data());
            uint32_t h = HardwareExtend(0, buf, data_1k.size());
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_1k.size());
    }

    static void BM_CRC32C_HW_4K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_4k.data());
            uint32_t h = HardwareExtend(0, buf, data_4k.size());
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_4k.size());
    }

    static void BM_CRC32C_HW_64K(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_64k.data());
            uint32_t h = HardwareExtend(0, buf, data_64k.size());
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_64k.size());
    }

    static void BM_CRC32C_HW_1M(benchmark::State& state)
    {
        for (auto _ : state)
        {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(data_1m.data());
            uint32_t h = HardwareExtend(0, buf, data_1m.size());
            benchmark::DoNotOptimize(h);
        }
        state.SetBytesProcessed(state.iterations() * data_1m.size());
    }
#endif

    BENCHMARK(BM_XXH32_64);
    BENCHMARK(BM_XXH64_64);
#if (defined(__x86_64__) && defined(__SSE4_2__)) || defined(__aarch64__)
    BENCHMARK(BM_CRC32C_HW_64);
#endif

    BENCHMARK(BM_XXH32_1K);
    BENCHMARK(BM_XXH64_1K);
#if (defined(__x86_64__) && defined(__SSE4_2__)) || defined(__aarch64__)
    BENCHMARK(BM_CRC32C_HW_1K);
#endif

    BENCHMARK(BM_XXH32_4K);
    BENCHMARK(BM_XXH64_4K);
#if (defined(__x86_64__) && defined(__SSE4_2__)) || defined(__aarch64__)
    BENCHMARK(BM_CRC32C_HW_4K);
#endif

    BENCHMARK(BM_XXH32_64K);
    BENCHMARK(BM_XXH64_64K);
#if (defined(__x86_64__) && defined(__SSE4_2__)) || defined(__aarch64__)
    BENCHMARK(BM_CRC32C_HW_64K);
#endif

    BENCHMARK(BM_XXH32_1M);
    BENCHMARK(BM_XXH64_1M);
#if (defined(__x86_64__) && defined(__SSE4_2__)) || defined(__aarch64__)
    BENCHMARK(BM_CRC32C_HW_1M);
#endif

} // namespace ctar

BENCHMARK_MAIN();
