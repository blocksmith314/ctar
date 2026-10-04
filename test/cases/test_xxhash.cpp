#include <cstring>
#include <gtest/gtest.h>
#include "src/core/options.h"
#include "xxhash.h"

namespace ctar
{
    TEST(XXH3_64, OneShotConsistent)
    {
        const char* data = "hello";
        size_t len = std::strlen(data);

        const uint64_t h1 = XXH64(data, len, kHashSeed);
        const uint64_t h2 = XXH64(data, len, kHashSeed);
        EXPECT_EQ(h1, h2);
    }

    TEST(XXH3_64, StreamEqualsOneShot)
    {
        const char* part1 = "hello ";
        const char* part2 = "CTAR";
        size_t l1 = std::strlen(part1);
        size_t l2 = std::strlen(part2);

        char full_buf[128];
        std::snprintf(full_buf, sizeof(full_buf), "%s%s", part1, part2);
        const uint64_t hash_once = XXH64(full_buf, l1 + l2, kHashSeed);

        XXH64_state_t* st = XXH64_createState();
        ASSERT_NE(st, nullptr);
        XXH64_reset(st, kHashSeed);
        XXH64_update(st, part1, l1);
        XXH64_update(st, part2, l2);
        const uint64_t hash_stream = XXH64_digest(st);
        XXH64_freeState(st);

        EXPECT_EQ(hash_once, hash_stream);
    }

    TEST(XXH3_64, EmptyInput)
    {
        uint64_t h_direct = XXH64(nullptr, 0, kHashSeed);

        XXH64_state_t* st = XXH64_createState();
        ASSERT_NE(st, nullptr);
        XXH64_reset(st, kHashSeed);
        uint64_t h_stream = XXH64_digest(st);
        XXH64_freeState(st);

        EXPECT_EQ(h_direct, h_stream);
    }

    TEST(XXH3_64, ResetReuseState)
    {
        XXH64_state_t* st = XXH64_createState();
        ASSERT_NE(st, nullptr);

        XXH64_reset(st, kHashSeed);
        XXH64_update(st, "aaa", 3);
        const uint64_t h1 = XXH64_digest(st);

        XXH64_reset(st, kHashSeed);
        XXH64_update(st, "bbb", 3);
        const uint64_t h2 = XXH64_digest(st);

        XXH64_freeState(st);
        EXPECT_NE(h1, h2);
    }

    TEST(XXH3_64, DifferentSeedDifferentHash)
    {
        const char* data = "test content";
        size_t len = std::strlen(data);
        uint64_t h_a = XXH64(data, len, 111);
        uint64_t h_b = XXH64(data, len, 222);
        EXPECT_NE(h_a, h_b);
    }
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}