#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "src/core/version.h"

namespace ctar
{
    TEST(Version, SerializeDeserialize)
    {
        constexpr uint8_t major = 1;
        constexpr uint8_t minor = 2;
        constexpr uint8_t patch = 3;
        constexpr Version version(major, minor, patch);
        constexpr uint32_t version_number = version.Encode();
        constexpr Version deser_version = Version::Decode(version_number);
        ASSERT_EQ(deser_version.major, major);
        ASSERT_EQ(deser_version.minor, minor);
        ASSERT_EQ(deser_version.patch, patch);
    }

    TEST(Version, ToString)
    {
        constexpr uint8_t major = 1;
        constexpr uint8_t minor = 2;
        constexpr uint8_t patch = 3;
        constexpr Version version(major, minor, patch);
        std::string_view expect_version = "1.2.3";
        const auto ver = version.to_string();
        EXPECT_EQ(ver, expect_version);
    }

    TEST(Version, Decode)
    {
        constexpr uint8_t major = 1;
        constexpr uint8_t minor = 2;
        constexpr uint8_t patch = 3;
        constexpr Version version(major, minor, patch);
        constexpr uint32_t version_int = version.Encode();
        EXPECT_EQ(version_int & 0xFFU, 1);
        EXPECT_EQ(version_int >> 8U & 0xFFU, 2);
        EXPECT_EQ(version_int >> 16U & 0xFFU, 3);
        EXPECT_EQ(version_int >> 24U & 0xFFU, 0);
    }

    TEST(Version, Default)
    {
        EXPECT_EQ(kToolVersion.major, kToolVersionMajor);
        EXPECT_EQ(kToolVersion.minor, kToolVersionMinor);
        EXPECT_EQ(kToolVersion.patch, kToolVersionPatch);

        EXPECT_EQ(kFileFormatVersion.major, kFileFormatVersionMajor);
        EXPECT_EQ(kFileFormatVersion.minor, kFileFormatVersionMinor);
        EXPECT_EQ(kFileFormatVersion.patch, kFileFormatVersionPatch);
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
