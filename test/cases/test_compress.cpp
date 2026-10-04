#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include "src/core/compress.h"
namespace ctar
{

    TEST(CompressTest, CompressAndDecompress)
    {
        const std::string input(1024 * 1024, 'A');
        const int input_size = static_cast<int>(input.size());
        const int dst_capacity = compress::MaxCompressBufferSize(CompressionType::kLZ4, input_size);
        std::string compressed(static_cast<size_t>(dst_capacity), '\0');
        const int compressed_size = compress::Compress(CompressionType::kLZ4, input.data(), compressed.data(),
                                                       input_size, static_cast<int>(compressed.size()));
        ASSERT_GT(compressed_size, 0);
        ASSERT_LT(compressed_size, input_size);
        std::string decompressed(input.size(), '\0');
        const int decompressed_size =
            compress::Decompress(CompressionType::kLZ4, compressed.data(), decompressed.data(), compressed_size,
                                 static_cast<int>(decompressed.size()));
        ASSERT_EQ(decompressed_size, input_size);
        EXPECT_EQ(decompressed, input);
    }

    TEST(CompressTest, CompressionTypeNone)
    {
        const std::string input(1024, 'A');
        const int input_size = static_cast<int>(input.size());
        const int dst_capacity = compress::MaxCompressBufferSize(CompressionType::kNone, input_size);
        ASSERT_EQ(dst_capacity, input_size);
        std::string compressed(static_cast<size_t>(dst_capacity), '\0');

        const int compressed_size = compress::Compress(CompressionType::kNone, input.data(), compressed.data(),
                                                       input_size, static_cast<int>(compressed.size()));
        ASSERT_EQ(compressed_size, input_size);
        ASSERT_EQ(compressed, input);
    }

    TEST(CompressTest, DecompressDifferentCompressionType)
    {
        const std::string input(1024 * 1024, 'A');
        const int input_size = static_cast<int>(input.size());

        const int dst_capacity = compress::MaxCompressBufferSize(CompressionType::kLZ4, input_size);
        std::string compressed(static_cast<size_t>(dst_capacity), '\0');
        const int compressed_size = compress::Compress(CompressionType::kLZ4, input.data(), compressed.data(),
                                                       input_size, static_cast<int>(compressed.size()));
        ASSERT_GT(compressed_size, 0);
        ASSERT_LT(compressed_size, input_size);
        std::string decompressed(input.size(), '\0');
        const int decompressed_size =
            compress::Decompress(CompressionType::kLZ4HC, compressed.data(), decompressed.data(), compressed_size,
                                 static_cast<int>(decompressed.size()));
        ASSERT_EQ(decompressed_size, input_size);
        EXPECT_EQ(decompressed, input);
    }

    TEST(CompressTest, EmptyData)
    {
        const int dst_capacity = compress::MaxCompressBufferSize(CompressionType::kLZ4, 0);
        const int compressed_size = compress::Compress(CompressionType::kLZ4, nullptr, nullptr, 0, dst_capacity);
        ASSERT_EQ(compressed_size, 0);
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
