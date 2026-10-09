#ifndef CTAR_OPTIONS_H
#define CTAR_OPTIONS_H

#include <array>
#include <cstdint>

namespace ctar
{

    // ascii(hexadecimal) of ctar 0x43544152
    static constexpr uint32_t MagicNumber = 0x52415443;

    namespace unit
    {
        constexpr size_t KiB = 1024ULL;
        constexpr size_t MiB = 1024ULL * 1024;
        constexpr size_t GiB = 1024ULL * 1024 * 1024;
        constexpr size_t TiB = 1024ULL * 1024 * 1024 * 1024;
    } // namespace unit

    enum class CompressionType : uint8_t
    {
        kNone = 0x0,
        kLZ4 = 0x1,
        kLZ4HC = 0x2,
    };

    constexpr int kLZ4AccDefault = 1;

    constexpr int kLZ4AccMax = 65537;

    constexpr int kLZ4HCDefault = 9;

    constexpr int kLZ4HCMin = 1;

    constexpr int kLZ4HCMax = 12;

    constexpr uint32_t kMaxCompressFileSize = 0x7E000000;

    constexpr size_t kMaxFileCountPerBlock = 128;

    constexpr size_t kMaxFileSizePerBlock = 128UL * unit::MiB;

    constexpr size_t kDefaultSplitFileSize = 4UL * unit::GiB;

    // File types that do not require compression
    constexpr auto kNoCompressSuffixes =
        std::array{".parquet", // column storage
                   ".jpeg",    ".jpg",  ".png", // image
                   ".h264",    ".h265", ".mp4", ".mkv", // video container
                   ".zip",     ".gz",   ".bz2", ".xz",  ".zst", ".rar", // already-compressed archive
                   ".mp3", // compressed audio
                   ".pdf"};

    struct CompressionConfig
    {
        CompressionType compression_type{CompressionType::kLZ4};

        // Algorithm-specific compression parameter
        // For LZ4 fast: acceleration value; For LZ4 HC: compression level
        int compression_param = kLZ4AccDefault;
    };

    // echo ctar | sha1sum
    // and taking the leading 64 bits.
    constexpr size_t kHashSeed = 0x19beca14c79d67faULL;

    // XXH64(nullptr, 0, kHashSeed);
    constexpr uint64_t kZeroXXHash = 0xe80e0f7d067306bbULL;

    // Maximum entries for one batch write
    constexpr size_t kMaxBatchWriteEntries = 4;

    // Default buffer size for holding all file/directory path name.
    constexpr size_t kEntryNameBufferSize = 1 * unit::MiB;

    // the size of write queue
    constexpr size_t kBlockBufferCount = 32;


} // namespace ctar

#endif
