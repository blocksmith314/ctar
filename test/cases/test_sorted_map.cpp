
#include <gtest/gtest.h>
#include <ranges>
#include <vector>
#include "src/core/sorted_map.h"

namespace ctar
{
    struct InnerStruct
    {
        int value1;
        int value2;
    };

    struct OuterStruct
    {
        int id;
        InnerStruct nested;
    };

    class UserId
    {
    public:
        UserId() = default;
        explicit UserId(const uint64_t id) : id_(id) {}
        auto operator<=>(const UserId& other) const = default;

    private:
        uint64_t id_ = 0;
    };

    // 故意不实现operator<=>，用于负向编译期约束测试
    struct BadKeyNoCompare
    {
        uint64_t val{};
    };

    TEST(SortedMapConstraints, StandardType)
    {
        static_assert(AllowedKeyType<uint64_t>, "Numeric types like uint64_t should be allowed as keys");

        static_assert(AllowedKeyType<int>, "Integer types should be allowed as keys");

        static_assert(!AllowedKeyType<std::string>, "std::string is not permitted for key");

        static_assert(!AllowedKeyType<BadKeyNoCompare>, "Type without operator<=> shall not be allowed as key");

        static_assert(AllowedValueType<int>, "Primitive types should be allowed as values");

        static_assert(AllowedValueType<std::vector<int>>, "Vectors with trivial elements should be allowed as values");

        static_assert(!AllowedValueType<std::vector<std::string>>,
                      "Vectors with non‑trivial elements (like std::string) should NOT be allowed");

        static_assert(!AllowedValueType<std::string>, "std::string should NOT be allowed");

        static_assert(AllowedValueType<std::array<int, 5>>, "Arrays with trivial elements should be allowed as values");
    }

    TEST(SortedMapConstraints, CustomType)
    {
        static_assert(AllowedKeyType<UserId>, "Custom UserId type with operator<=> must satisfy AllowedKeyType");

        static_assert(AllowedValueType<InnerStruct>,
                      "Simple trivial struct (InnerStruct) must satisfy AllowedValueType");

        static_assert(AllowedValueType<OuterStruct>,
                      "Nested trivial struct (OuterStruct) must satisfy AllowedValueType");
    }

    TEST(SortedMap, PutOrderedInsert)
    {
        SortedMap<int, int> order_map;
        order_map.put(1, 10);
        order_map.put(5, 50);
        order_map.put(2, 20);

        std::vector<int> keys;
        std::vector<int> values;
        const std::vector<int> expected_keys = {1, 2, 5};
        const std::vector<int> expected_values = {10, 20, 50};

        for (const auto& pair : order_map)
        {
            keys.emplace_back(pair.first);
            values.emplace_back(pair.second);
        }

        EXPECT_EQ(keys, expected_keys);
        EXPECT_EQ(values, expected_values);
        EXPECT_EQ(order_map.size(), 3U);
        EXPECT_FALSE(order_map.empty());
    }

    TEST(SortedMap, PutDuplicateKeyOverwrite)
    {
        SortedMap<int, int> map;
        map.put(3, 100);
        map.put(3, 999);

        auto it = map.find(3);
        ASSERT_NE(it, map.end());
        EXPECT_EQ(it->second, 999);
        EXPECT_EQ(map.size(), 1U);
    }

    TEST(SortedMap, FindKeyExistAndMissing)
    {
        SortedMap<int, int> map;
        map.put(2, 200);
        map.put(7, 700);

        auto it_ok = map.find(2);
        ASSERT_NE(it_ok, map.end());
        EXPECT_EQ(it_ok->second, 200);

        auto it_miss = map.find(99);
        EXPECT_EQ(it_miss, map.end());
    }

    TEST(SortedMap, EmptyContainerIterate)
    {
        SortedMap<int, int> empty_map;
        EXPECT_EQ(empty_map.begin(), empty_map.end());
        EXPECT_TRUE(empty_map.empty());
        EXPECT_EQ(empty_map.size(), 0U);

        auto it = empty_map.find(123);
        EXPECT_EQ(it, empty_map.end());
    }

    TEST(SortedMap, ReverseOrderInsert)
    {
        SortedMap<int, int> map;
        map.put(10, 1);
        map.put(5, 2);
        map.put(1, 3);

        std::vector<int> keys;
        for (const auto& p : map)
        {
            keys.push_back(p.first);
        }
        const std::vector<int> expect{1, 5, 10};
        EXPECT_EQ(keys, expect);
    }

    TEST(SortedMap, ConstIteratorTraverse)
    {
        SortedMap<int, int> map;
        map.put(10, 100);
        map.put(5, 50);

        const SortedMap<int, int>& const_ref = map;
        std::vector<int> keys;
        for (const auto& p : const_ref)
        {
            keys.push_back(p.first);
        }
        EXPECT_EQ(keys, std::vector<int>({5, 10}));
    }

    TEST(SortedMap, TrivialStructValuePutFind)
    {
        SortedMap<UserId, OuterStruct> map;
        UserId k1{100};
        OuterStruct v1{1, {10, 20}};
        map.put(k1, v1);

        auto it = map.find(k1);
        ASSERT_NE(it, map.end());
        EXPECT_EQ(it->second.id, 1);
        EXPECT_EQ(it->second.nested.value1, 10);
        EXPECT_EQ(it->second.nested.value2, 20);
    }

    TEST(SortedMap, CopyMoveSemantic)
    {
        SortedMap<int, int> src;
        src.put(1, 10);
        src.put(2, 20);

        // copy
        SortedMap<int, int> copy = src;
        EXPECT_EQ(copy.size(), 2U);
        EXPECT_NE(copy.find(1), copy.end());

        // move
        SortedMap<int, int> moved = std::move(src);
        EXPECT_EQ(moved.size(), 2U);
        EXPECT_NE(moved.find(2), moved.end());
    }
} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}