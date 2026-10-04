#include "gtest/gtest.h"
#include "src/utils/serialize_helper.h"

namespace ctar
{
    struct AlignParam
    {
        uint64_t input;
        uint64_t expect;
    };

    class Next4kAlignTest : public ::testing::TestWithParam<AlignParam>
    {
    };

    TEST_P(Next4kAlignTest, CheckAlignValue)
    {
        auto param = GetParam();
        const uint64_t actual = ctar::next_4k_align(param.input);
        ASSERT_EQ(actual, param.expect) << "input = " << param.input;
    }

    INSTANTIATE_TEST_SUITE_P(AlignCases, Next4kAlignTest,
                             ::testing::Values(AlignParam{0, 0}, AlignParam{4095, 4096}, AlignParam{4096, 4096},
                                               AlignParam{4096 + 1, 4096 * 2}, AlignParam{4096 * 3 - 1, 4096 * 3},
                                               AlignParam{4096 * 3 + 1, 4096 * 4}));

    struct TestStruct
    {
        uint8_t a;
        uint16_t b;
        uint32_t c;
        uint64_t d;
    };

    struct NestedStruct
    {
        int32_t  x;
        TestStruct inner;
    };

    TEST(FieldSize, FixedFieldSize)
    {
        TestStruct obj;
        ASSERT_EQ(FixedFieldTotalSize(obj.a), 1);
        ASSERT_EQ(FixedFieldTotalSize(obj.b), 2);
        ASSERT_EQ(FixedFieldTotalSize(obj.c), 4);
        ASSERT_EQ(FixedFieldTotalSize(obj.d), 8);

        ASSERT_EQ(FixedFieldTotalSize(obj.a, obj.b, obj.c, obj.d), 15);
    }

    TEST(FieldSize, NestedStructureMember)
    {
        constexpr NestedStruct ns{};
        // int32_t(4) + 15 = 19
        ASSERT_EQ(FixedFieldTotalSize(ns.x, ns.inner.a, ns.inner.b, ns.inner.c, ns.inner.d), 19U);
    }

    TEST(SerializeSize,VectorUint8)
    {
        std::vector<uint8_t> vec{1,2,3};
        ASSERT_EQ(ContainerSerializedSize(vec), 8U + 3U);
    }


    TEST(SerializeSize, VectorUint64)
    {
        std::vector<uint64_t> vec(4);
        // 8 + 4*8 = 40
        ASSERT_EQ(ContainerSerializedSize(vec), 8U + 4U * 8U);
    }

    TEST(SerializeSize, MultiContainerSum)
    {
        std::vector<uint8_t> v1(2);
        std::vector<uint16_t> v2(3);
        // v1:8+2; v2:8+3*2=14; total 10+14=24
        ASSERT_EQ(ContainerSerializedTotalSize(v1,v2), 24U);
    }

    TEST(SerializeSize, StringContainer)
    {
        std::string str{"hello"};
        // char元素，长度5；8+5 =13
        ASSERT_EQ(ContainerSerializedSize(str), 8U + 5U);
    }


    TEST(SkipJunkFile,HitJunkFile)
    {
        fs::directory_entry entry1{"tmp/.DS_Store"};
        EXPECT_TRUE(is_skip_entry(entry1));

        fs::directory_entry entry2{"tmp/.other"};
        EXPECT_FALSE(is_skip_entry(entry2));

    }


    TEST(PermBitsToString, BasicCases)
    {
        // 0644 rw-r--r--
        EXPECT_EQ(PermBitsToString(0644), "rw-r--r--");
        // 0755 rwxr-xr-x
        EXPECT_EQ(PermBitsToString(0755), "rwxr-xr-x");
        // 0777 rwxrwxrwx
        EXPECT_EQ(PermBitsToString(0777), "rwxrwxrwx");
        // 0000 ---------
        EXPECT_EQ(PermBitsToString(0000), "---------");
        // 0600 rw-------
        EXPECT_EQ(PermBitsToString(0600), "rw-------");
        // 0700 rwx------
        EXPECT_EQ(PermBitsToString(0700), "rwx------");
    }

    TEST(FormatTime, BasicCases)
    {
        EXPECT_EQ(FormatTime(0), "Jan 01  1970");
        EXPECT_EQ(FormatTime(3600 * 24), "Jan 02  1970");

        const long long now_sec = static_cast<long long>(std::time(nullptr));
        std::string out = FormatTime(now_sec);
        EXPECT_EQ(out.size(), 12U);
        EXPECT_TRUE(out.find(':') != std::string::npos);
    }
}


int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
