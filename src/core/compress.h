#ifndef CTAR_LZ4_COMPAT_H
#define CTAR_LZ4_COMPAT_H
#include <algorithm>
#include <cstring>

#include "lz4.h"
#include "lz4hc.h"
#include "options.h"
namespace ctar
{
    constexpr bool ShouldSkipCompression(std::string_view filename)
    {
        return std::ranges::any_of(kNoCompressSuffixes,
                                   [filename](std::string_view suffix) { return filename.ends_with(suffix); });
    }

    static_assert(kLZ4HCDefault == LZ4HC_CLEVEL_DEFAULT, "kLZ4HCDefault mismatch with LZ4HC_CLEVEL_DEFAULT");
    static_assert(kLZ4HCMax == LZ4HC_CLEVEL_MAX, "kLZ4HCMax mismatch with LZ4HC_CLEVEL_MAX");

    namespace compress
    {
        inline uint64_t MaxCompressBufferSize(const CompressionType compression_type, const uint64_t src_size)
        {
            switch (compression_type)
            {
            case CompressionType::kLZ4:
            case CompressionType::kLZ4HC:
                return LZ4_compressBound(static_cast<int>(src_size));
            case CompressionType::kNone:
                return src_size;
            default:
                return 0;
            }
        }

        inline uint64_t Compress(const CompressionType compression_type, const char* src, char* dst,
                                 const uint64_t src_size, const int compression_param = 1)
        {
            if (src == nullptr || dst == nullptr)
                return 0;
            switch (compression_type)
            {
            case CompressionType::kLZ4:
                {
                    // acceleration between  1 and 65537
                    const int dst_capacity = LZ4_compressBound(static_cast<int>(src_size));
                    const int compressed_size =
                        LZ4_compress_fast(src, dst, static_cast<int>(src_size), dst_capacity, compression_param);
                    return compressed_size;
                }
            case CompressionType::kLZ4HC:
                {
                    // compressionLevel  between 1 and 12
                    const int dst_capacity = LZ4_compressBound(static_cast<int>(src_size));
                    const int compressed_size =
                        LZ4_compress_HC(src, dst, static_cast<int>(src_size), dst_capacity, compression_param);
                    return compressed_size;
                }
            case CompressionType::kNone:
                {
                    std::memcpy(dst, src, src_size);
                    return src_size;
                }
            default:
                return 0;
            }
        }

        inline uint64_t Decompress(const CompressionType compression_type, const char* src, char* dst,
                                   const uint64_t compressed_size, const uint64_t dst_capacity)
        {
            switch (compression_type)
            {
            case CompressionType::kLZ4:
            case CompressionType::kLZ4HC:
                {
                    const int decompressed_size = LZ4_decompress_safe(src, dst, static_cast<int>(compressed_size),
                                                                      static_cast<int>(dst_capacity));
                    return decompressed_size;
                }
            case CompressionType::kNone:
                {
                    std::memcpy(dst, src, compressed_size);
                    return compressed_size;
                }
            default:
                return 0;
            }
        }
    } // namespace compress
} // namespace ctar
#endif // CTAR_LZ4_COMPAT_H
