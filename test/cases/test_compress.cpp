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

    TEST(CompressTest, MaxCompressBufferSizeAboveLz4InputLimit)
    {
        // Sizes past LZ4_MAX_INPUT_SIZE used to wrap through LZ4's int-based API and
        // collapse to 0, which left the block buffer too small for the verbatim copy.
        constexpr uint64_t oversized = static_cast<uint64_t>(LZ4_MAX_INPUT_SIZE) + 4096;
        ASSERT_EQ(compress::MaxCompressBufferSize(CompressionType::kLZ4, oversized), oversized);
        ASSERT_EQ(compress::MaxCompressBufferSize(CompressionType::kLZ4HC, oversized), oversized);
        ASSERT_EQ(compress::MaxCompressBufferSize(CompressionType::kNone, oversized), oversized);

        // At the limit the real LZ4 bound still applies and leaves room for the frame.
        ASSERT_GT(compress::MaxCompressBufferSize(CompressionType::kLZ4, LZ4_MAX_INPUT_SIZE),
                  static_cast<uint64_t>(LZ4_MAX_INPUT_SIZE));
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
