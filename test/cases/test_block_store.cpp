#include <csignal>
#include <fstream>
#include <iterator>
#include <random>
#include <gtest/gtest.h>
#include <ranges>
#include <string>

#include "src/core/block_store.h"
#include "src/core/file_handle.h"
#include "src/utils/coding.h"
#include "test/tools/dir_compare.h"
#include "test/tools/file_generator.h"
#include "xxhash.h"
#include "test/tools/fixture/blob_store_fixture.h"
#include "test/tools/test_helpers.h"

namespace ctar
{

    /// @brief Verify PACK file magic‑number integrity (head & tail magic)
    /// @param pack_file path to pack file
    /// @return GTest AssertionResult, use with ASSERT_TRUE / EXPECT_TRUE
    testing::AssertionResult VerifyPACKFileIntegrity(const std::string& pack_file)
    {
        auto read_file_status = NewPosixReadFile(pack_file);
        if (!read_file_status)
        {
            return testing::AssertionFailure() << "NewPosixReadFile failed for file: " << pack_file;
        }
        auto& read_file = read_file_status.value();

        constexpr size_t magic_len = sizeof(kMagicNumber);

        // read head magic: offset 0
        std::string buf;
        buf.resize(magic_len);
        auto read_status = read_file->RandomRead(0, magic_len, buf.data());
        if (!read_status)
        {
            return testing::AssertionFailure() << "RandomRead head magic failed, file: " << pack_file;
        }
        if (read_status.value() != magic_len)
        {
            return testing::AssertionFailure() << "Head magic short‑read. expected=" << magic_len
                                               << ", actual=" << read_status.value() << ", file=" << pack_file;
        }
        uint32_t head_magic = DecodeFixed32(buf.data());
        if (head_magic != kMagicNumber)
        {
            return testing::AssertionFailure() << "Head magic mismatch! expected=0x" << std::hex << kMagicNumber
                                               << ", actual=0x" << std::hex << head_magic << ", file=" << pack_file;
        }

        // get file size for tail magic offset
        auto file_size_status = read_file->GetFileSize();
        if (!file_size_status)
        {
            return testing::AssertionFailure() << "GetFileSize failed, file: " << pack_file;
        }
        const auto file_sz = static_cast<off_t>(file_size_status.value());
        const off_t tail_magic_offset = file_sz - static_cast<off_t>(magic_len);
        if (tail_magic_offset < 0)
        {
            return testing::AssertionFailure() << "File too small for tail magic, size=" << file_sz
                                               << ", magic_len=" << magic_len << ", file=" << pack_file;
        }

        // read tail magic
        buf.resize(magic_len);
        read_status = read_file->RandomRead(tail_magic_offset, magic_len, buf.data());
        if (!read_status)
        {
            return testing::AssertionFailure()
                << "RandomRead tail magic failed, offset=" << tail_magic_offset << ", file=" << pack_file;
        }
        if (read_status.value() != magic_len)
        {
            return testing::AssertionFailure()
                << "Tail magic short‑read. expected=" << magic_len << ", actual=" << read_status.value()
                << ", offset=" << tail_magic_offset << ", file=" << pack_file;
        }
        if (uint32_t tail_magic = DecodeFixed32(buf.data()); tail_magic != kMagicNumber)
        {
            return testing::AssertionFailure()
                << "Tail magic mismatch! expected=0x" << std::hex << kMagicNumber << ", actual=0x" << std::hex
                << tail_magic << ", offset=" << tail_magic_offset << ", file=" << pack_file;
        }

        return testing::AssertionSuccess();
    }


    class BlockStorePresetFileTest : public test::PresetFileFixture
    {
    };

    class BlockStoreRandomFileTest : public ::testing::Test
    {
    public:
        fs::path source_dir_{"random_file_test_dir"};
        fs::path pack_dir_{"random_target_test_dir"};
        std::string pack_file = pack_dir_ / "random_file.ctar";
        fs::path unpack_dir_{"random_unpack_test_dir"};

        test::GeneratorOption option_;

    protected:
        void SetUp() override
        {
            if (fs::exists(source_dir_))
            {
                fs::remove_all(source_dir_);
            }

            if (fs::exists(pack_dir_))
            {
                fs::remove_all(pack_dir_);
            }

            if (fs::exists(unpack_dir_))
            {
                fs::remove_all(unpack_dir_);
            }

            fs::create_directories(source_dir_);
            fs::create_directories(pack_dir_);
            fs::create_directories(unpack_dir_);

            test::FileGenerator generator_random(source_dir_);
            option_.first_level_dir_count = 10;
            option_.max_dir_depth = 5;
            option_.file_count = 1000;
            option_.min_file_size = 128;
            option_.max_file_size = 2048000;
            option_.create_junk_file = false;

            if (auto st = generator_random.GenerateRandomFiles(option_);
                !st)
            {
                throw std::runtime_error("failed to generate preset files");
            }

            std::println(std::cout, "generator count of file: {}, file_size: {}", generator_random.file_cnt,
                         generator_random.total_file_size);
        }

        void TearDown() override
        {
            fs::remove_all(source_dir_);
            fs::remove_all(pack_dir_);
            fs::remove_all(unpack_dir_);
        }
    };


    TEST_F(BlockStorePresetFileTest, ScanDirctory)
    {
        BlockStore blob_store;
        Status status = blob_store.Scan(source_dir_);
        ASSERT_TRUE(status);
        std::ostringstream oss;
        status = blob_store.RenderDirectoryTree(oss);
        ASSERT_TRUE(status);
        std::string tree_text = oss.str();
        test::StringContainsAll(tree_text, preset_key_words);
    }

    TEST_F(BlockStorePresetFileTest, PackAndUnPack)
    {
        {
            BlockStore blob_store;
            auto status = blob_store.Scan(source_dir_);
            ASSERT_TRUE(status) << "failed to scan source directory: " << status.error().message();
            status = blob_store.PackPipeline(pack_file);
            ASSERT_TRUE(status) << "failed to save packed file: " << status.error().message();
            ASSERT_TRUE(VerifyPACKFileIntegrity(pack_file));
        }

        {
            BlockStore blob_store;
            auto status = blob_store.UnPackPipeline(pack_file, unpack_dir_);
            ASSERT_TRUE(status) << "failed to restore files: " << status.error().message();
        }
        {
            bool is_equal = test::AreDirectoriesEqual(source_dir_, unpack_dir_ / source_dir_);
            ASSERT_TRUE(is_equal) << "the dir is not match";
        }
    }


    TEST_F(BlockStoreRandomFileTest, PackAndUnPack)
    {
        {
            BlockStore blob_store;
            auto status = blob_store.Scan(source_dir_);
            ASSERT_TRUE(status) << "failed to scan source directory: " << status.error().message();
            status = blob_store.PackPipeline(pack_file);
            ASSERT_TRUE(status) << "failed to save packed file: " << status.error().message();
            ASSERT_TRUE(VerifyPACKFileIntegrity(pack_file));
        }
        {
            BlockStore blob_store;
            auto status = blob_store.UnPackPipeline(pack_file, unpack_dir_);
            ASSERT_TRUE(status) << "failed to restore files: " << status.error().message();
        }
        {
            bool is_equal = test::AreDirectoriesEqual(source_dir_, unpack_dir_ / source_dir_);
            ASSERT_TRUE(is_equal) << "the dir is not match";
        }
    }


    // Every block of this tree has a zero-length payload: the writer must treat that as
    // "nothing to write" instead of issuing a zero-length iovec.
    class BlockStoreEmptyFileTest : public ::testing::Test
    {
    public:
        fs::path source_dir_{"empty_file_test_dir"};
        fs::path pack_dir_{"empty_file_target_test_dir"};
        fs::path unpack_dir_{"empty_file_unpack_test_dir"};
        std::string pack_file = (pack_dir_ / "empty_file.ctar").string();

    protected:
        void SetUp() override
        {
            fs::remove_all(source_dir_);
            fs::remove_all(pack_dir_);
            fs::remove_all(unpack_dir_);
            fs::create_directories(source_dir_ / "nested");
            fs::create_directories(pack_dir_);
            fs::create_directories(unpack_dir_);

            for (const char* name : {"a.txt", "b.txt", "c.txt", "nested/d.bin", "nested/e.log"})
            {
                std::ofstream ofs(source_dir_ / name, std::ios::binary | std::ios::trunc);
                ASSERT_TRUE(ofs.good()) << "failed to create " << name;
            }
        }

        void TearDown() override
        {
            fs::remove_all(source_dir_);
            fs::remove_all(pack_dir_);
            fs::remove_all(unpack_dir_);
        }
    };

    TEST_F(BlockStoreEmptyFileTest, PackAndUnPackTreeOfEmptyFiles)
    {
        {
            BlockStore blob_store;
            auto status = blob_store.Scan(source_dir_);
            ASSERT_TRUE(status) << "failed to scan source directory: " << status.error().message();
            status = blob_store.PackPipeline(pack_file);
            ASSERT_TRUE(status) << "failed to pack empty files: " << status.error().message();
            ASSERT_TRUE(VerifyPACKFileIntegrity(pack_file));
        }
        {
            BlockStore blob_store;
            auto status = blob_store.UnPackPipeline(pack_file, unpack_dir_);
            ASSERT_TRUE(status) << "failed to restore empty files: " << status.error().message();
        }
        ASSERT_TRUE(test::AreDirectoriesEqual(source_dir_, unpack_dir_ / source_dir_))
            << "the dir is not match";
    }

    TEST(ArchivePath, ResolvesEntriesInsideTheOutputDirectory)
    {
        const fs::path root = "/tmp/out";

        auto inside = ResolveArchiveDirectory(root, "a/b");
        ASSERT_TRUE(inside) << inside.error().message();
        EXPECT_EQ(inside.value(), fs::path("/tmp/out/a/b"));

        // An archive that recorded an absolute path still lands inside the output dir.
        auto absolute = ResolveArchiveDirectory(root, "/data/tree");
        ASSERT_TRUE(absolute) << absolute.error().message();
        EXPECT_EQ(absolute.value(), fs::path("/tmp/out/data/tree"));

        // ".." that resolves back inside the output directory is harmless.
        auto back = ResolveArchiveDirectory(root, "a/../b");
        ASSERT_TRUE(back) << back.error().message();
        EXPECT_EQ(back.value(), fs::path("/tmp/out/b"));

        auto file = ResolveArchiveFile(root, "a", "x.txt");
        ASSERT_TRUE(file) << file.error().message();
        EXPECT_EQ(file.value(), fs::path("/tmp/out/a/x.txt"));
    }

    TEST(ArchivePath, RejectsDirectoryTraversal)
    {
        const fs::path root = "/tmp/out";
        EXPECT_FALSE(ResolveArchiveDirectory(root, "../etc"));
        EXPECT_FALSE(ResolveArchiveDirectory(root, "a/../../etc"));
        EXPECT_FALSE(ResolveArchiveDirectory(root, ""));
    }

    TEST(ArchivePath, RejectsFileNameTraversal)
    {
        const fs::path root = "/tmp/out";
        EXPECT_FALSE(ResolveArchiveFile(root, "a", ".."));
        EXPECT_FALSE(ResolveArchiveFile(root, "a", "../x"));
        EXPECT_FALSE(ResolveArchiveFile(root, "a", "/etc/passwd"));
        EXPECT_FALSE(ResolveArchiveFile(root, "a", "b/c"));
        EXPECT_FALSE(ResolveArchiveFile(root, "a", ""));
    }

    TEST(BlockStoreHash, FileHashMatchesInlineHash)
    {
        const fs::path file = "hash_file_test.bin";
        const std::string content = "ctar hash test\n";
        {
            std::ofstream ofs(file, std::ios::binary | std::ios::trunc);
            ofs << content;
        }

        auto from_file = Hash(file.string());
        ASSERT_TRUE(from_file) << from_file.error().message();
        EXPECT_EQ(from_file.value(), XXH64(content.data(), content.size(), kHashSeed));

        fs::remove(file);
    }

    TEST(BlockStoreHash, EmptyAndMissingFile)
    {
        const fs::path empty = "hash_empty_test.bin";
        {
            std::ofstream ofs(empty, std::ios::binary | std::ios::trunc);
        }
        auto empty_hash = Hash(empty.string());
        ASSERT_TRUE(empty_hash) << empty_hash.error().message();
        EXPECT_EQ(empty_hash.value(), XXH64(nullptr, 0, kHashSeed));
        fs::remove(empty);

        // A missing file is reported as an error, not a crash.
        EXPECT_FALSE(Hash("hash_missing_file.bin"));
    }



    namespace
    {
        std::string ReadWholeFile(const std::string& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        void WriteWholeFile(const std::string& path, const std::string& data)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }

        uint64_t ReadU64At(const std::string& data, size_t offset)
        {
            uint64_t value = 0;
            std::memcpy(&value, data.data() + offset, sizeof(value));
            return value;
        }

        void WriteU64At(std::string& data, size_t offset, uint64_t value)
        {
            std::memcpy(data.data() + offset, &value, sizeof(value));
        }

        /// A tiny tree packed once, so each malformed-archive case only has to patch bytes.
        struct MalformedArchiveFixture
        {
            fs::path root{"malformed_archive_test"};
            fs::path source_dir{root / "source"};
            fs::path archive{root / "source.ctar"};
            fs::path out_dir{root / "out"};

            void Create()
            {
                fs::remove_all(root);
                fs::create_directories(source_dir / "sub");
                std::ofstream(source_dir / "a.txt") << "hello";
                std::ofstream(source_dir / "sub" / "b.bin") << "world";
                BlockStore store;
                ASSERT_TRUE(store.Scan(source_dir.string()));
                ASSERT_TRUE(store.PackPipeline(archive.string()));
            }

            [[nodiscard]] std::string Bytes() const { return ReadWholeFile(archive.string()); }

            Status Unpack(const std::string& bytes)
            {
                WriteWholeFile(archive.string(), bytes);
                fs::remove_all(out_dir);
                BlockStore store;
                return store.UnPackPipeline(archive.string(), out_dir.string());
            }

            void Cleanup() const { fs::remove_all(root); }
        };
    } // namespace

    TEST(MalformedArchive, CraftedMetadataIsRejected)
    {
        MalformedArchiveFixture fixture;
        fixture.Create();
        const std::string good = fixture.Bytes();
        ASSERT_GT(good.size(), 32U);

        // header: magic(4) version(4); 1.1.0 metadata lives at the end.
        constexpr size_t kVersionOffset = 4;
        constexpr size_t kMetadataOffsetOffset = 96;
        constexpr size_t kMetadataOriginalSizeOffset = 104;
        constexpr size_t kMetadataCompressedSizeOffset = 112;
        constexpr size_t kFileDataOffsetOffset = 64;
        const size_t metadata_offset = ReadU64At(good, kMetadataOffsetOffset);
        const size_t metadata_size = ReadU64At(good, kMetadataCompressedSizeOffset);
        const size_t file_data_offset = ReadU64At(good, kFileDataOffsetOffset);
        ASSERT_EQ(file_data_offset, PackFileHeader::kSerializedSize);
        ASSERT_EQ(metadata_offset + metadata_size + sizeof(kMagicNumber), good.size());
        const size_t dir_name_size_offset = metadata_offset + 4 * sizeof(uint64_t);
        const uint64_t dir_name_size = ReadU64At(good, dir_name_size_offset);
        const size_t file_name_size_offset = dir_name_size_offset + sizeof(uint64_t) + dir_name_size;
        const uint64_t file_name_size = ReadU64At(good, file_name_size_offset);
        const size_t first_map_count_offset = file_name_size_offset + sizeof(uint64_t) + file_name_size;
        ASSERT_LT(first_map_count_offset + sizeof(uint64_t), good.size());

        const auto expect_rejected = [&](std::string bytes, std::string_view what)
        {
            const Status status = fixture.Unpack(bytes);
            EXPECT_FALSE(status) << what << " should be rejected";
            if (!status)
            {
                EXPECT_FALSE(status.error().message().empty()) << what;
            }
        };

        {
            std::string bytes = good;
            WriteU64At(bytes, kMetadataCompressedSizeOffset, 0x7FFFFFFFFFFFFFFFULL);
            expect_rejected(bytes, "huge metadata size");
        }
        {
            std::string bytes = good;
            WriteU64At(bytes, kMetadataOriginalSizeOffset, 0);
            WriteU64At(bytes, kMetadataCompressedSizeOffset, 0);
            expect_rejected(bytes, "empty metadata size");
        }
        {
            std::string bytes = good;
            WriteU64At(bytes, first_map_count_offset, 1ULL << 40);
            expect_rejected(bytes, "huge entry count");
        }
        {
            std::string bytes = good;
            const uint32_t newer_format = Version{2, 0, 0}.Encode();
            std::memcpy(bytes.data() + kVersionOffset, &newer_format, sizeof(newer_format));
            const Status status = fixture.Unpack(bytes);
            ASSERT_FALSE(status);
            EXPECT_NE(status.error().message().find("format"), std::string::npos) << status.error().message();
        }
        fixture.Cleanup();
    }

    TEST(MalformedArchive, SurvivesRandomMetadataMutations)
    {
        MalformedArchiveFixture fixture;
        fixture.Create();
        const std::string good = fixture.Bytes();
        const size_t metadata_offset = ReadU64At(good, 96);
        const size_t metadata_size = ReadU64At(good, 112);
        const size_t meta_end = metadata_offset + metadata_size;
        ASSERT_LT(meta_end, good.size());

        std::mt19937 rng(20261108);
        std::uniform_int_distribution<size_t> offset_dist(0, meta_end - 1);
        for (int i = 0; i < 100; ++i)
        {
            std::string bytes = good;
            const size_t offset = offset_dist(rng);
            bytes[offset] = static_cast<char>(bytes[offset] ^ 0xFF);
            try
            {
                const Status status = fixture.Unpack(bytes);
                (void)status; // success or failure is fine; crashing is not
            }
            catch (const std::exception&)
            {
                // An exception escaping the library is acceptable: the CLI catches it.
            }
        }
        fixture.Cleanup();
    }


    TEST(MalformedArchive, RejectsNoneFileWithOversizedCompressedSize)
    {
        MalformedArchiveFixture fixture;
        fixture.Create();
        std::string bytes = fixture.Bytes();

        constexpr size_t kMetadataOffsetField = 96;
        constexpr size_t kMetadataSizeField = 112;
        constexpr size_t kMetadataTypeField = 120;
        constexpr size_t kMetadataChecksumField = 128;

        const size_t metadata_offset = ReadU64At(bytes, kMetadataOffsetField);
        const size_t metadata_size = ReadU64At(bytes, kMetadataSizeField);
        ASSERT_GT(metadata_size, 0U);
        ASSERT_EQ(ReadU64At(bytes, kMetadataTypeField), static_cast<uint64_t>(CompressionType::kNone))
            << "the fixture archive is expected to store metadata raw";

        size_t pos = metadata_offset;
        const size_t metadata_end = metadata_offset + metadata_size;
        const auto read_u64 = [&]()
        {
            EXPECT_LE(pos + sizeof(uint64_t), metadata_end);
            const uint64_t value = ReadU64At(bytes, pos);
            pos += sizeof(uint64_t);
            return value;
        };

        // PackFileMeta: four scalar fields, then the two name buffers.
        for (int i = 0; i < 4; ++i)
        {
            (void)read_u64();
        }
        pos += static_cast<size_t>(read_u64()); // dir_name_data
        pos += static_cast<size_t>(read_u64()); // file_name_data

        // dir_entry_records / file_entry_records: count * (key + EntryRecord).
        constexpr size_t kEntryRecordSize = 2 * sizeof(uint64_t) + 176;
        for (int map = 0; map < 2; ++map)
        {
            const uint64_t count = read_u64();
            pos += static_cast<size_t>(count) * (sizeof(uint64_t) + kEntryRecordSize);
        }

        // The three id -> vector maps: count * (key + value_count + values).
        for (int map = 0; map < 3; ++map)
        {
            const uint64_t count = read_u64();
            for (uint64_t i = 0; i < count; ++i)
            {
                (void)read_u64(); // key
                const uint64_t value_count = read_u64();
                pos += static_cast<size_t>(value_count) * sizeof(uint64_t);
            }
        }

        const uint64_t block_count = read_u64();
        ASSERT_GE(block_count, 1U);
        (void)read_u64(); // block_id
        (void)read_u64(); // dir_id
        (void)read_u64(); // block_file_offset
        const uint64_t payload_size = read_u64();
        (void)read_u64(); // block_size
        (void)read_u64(); // hash_value
        ASSERT_GT(payload_size, 1U);

        const uint64_t file_count = read_u64();
        ASSERT_GE(file_count, 1U);
        pos += static_cast<size_t>(file_count) * sizeof(uint64_t); // file_ids

        const uint64_t orig_count = read_u64();
        ASSERT_EQ(orig_count, file_count);
        const size_t orig_sizes_offset = pos;
        pos += static_cast<size_t>(orig_count) * sizeof(uint64_t);

        const uint64_t compressed_count = read_u64();
        ASSERT_EQ(compressed_count, file_count);
        const size_t compressed_sizes_offset = pos;
        pos += static_cast<size_t>(compressed_count) * sizeof(uint64_t);

        const uint64_t type_count = read_u64();
        ASSERT_EQ(type_count, file_count);
        const size_t types_offset = pos;
        pos += static_cast<size_t>(type_count);
        ASSERT_LE(pos, metadata_end);

        // Record the first file's offset so the crafted entry cannot trip the
        // offset/payload check before the kNone size check under test.
        const uint64_t offsets_count = read_u64();
        ASSERT_EQ(offsets_count, file_count);
        ASSERT_EQ(read_u64(), 0U) << "the crafted entry must be the first file in the block";

        // First file: raw storage whose declared payload is larger than the one-byte
        // buffer unpack would allocate for it.
        WriteU64At(bytes, orig_sizes_offset, 1);
        WriteU64At(bytes, compressed_sizes_offset, payload_size);
        bytes[types_offset] = static_cast<char>(CompressionType::kNone);

        const std::string metadata = bytes.substr(metadata_offset, metadata_size);
        WriteU64At(bytes, kMetadataChecksumField, XXH64(metadata.data(), metadata.size(), kHashSeed));

        const Status status = fixture.Unpack(bytes);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
        // Rejection has to come from the kNone cross-field check, not from the size
        // check that runs after the unsafe copy. The message names the stored path.
        EXPECT_NE(status.error().err_message.find("a.txt"), std::string::npos) << status.error().message();
        EXPECT_NE(status.error().err_message.find("raw entry stored size"), std::string::npos)
            << status.error().message();
        EXPECT_NE(status.error().err_message.find("!= original size 1"), std::string::npos)
            << status.error().message();

        fixture.Cleanup();
    }

    TEST(MalformedArchive, RejectsBlockMetaThatDisagreesWithEntryIndex)
    {
        MalformedArchiveFixture fixture;
        fixture.Create();
        std::string bytes = fixture.Bytes();

        constexpr size_t kMetadataOffsetField = 96;
        constexpr size_t kMetadataSizeField = 112;
        constexpr size_t kMetadataTypeField = 120;
        constexpr size_t kMetadataChecksumField = 128;

        const size_t metadata_offset = ReadU64At(bytes, kMetadataOffsetField);
        const size_t metadata_size = ReadU64At(bytes, kMetadataSizeField);
        ASSERT_GT(metadata_size, 0U);
        ASSERT_EQ(ReadU64At(bytes, kMetadataTypeField), static_cast<uint64_t>(CompressionType::kNone));

        size_t pos = metadata_offset;
        const size_t metadata_end = metadata_offset + metadata_size;
        const auto read_u64 = [&]()
        {
            EXPECT_LE(pos + sizeof(uint64_t), metadata_end);
            const uint64_t value = ReadU64At(bytes, pos);
            pos += sizeof(uint64_t);
            return value;
        };

        // PackFileMeta: four scalar fields, then the two name buffers.
        for (int i = 0; i < 4; ++i)
        {
            (void)read_u64();
        }
        pos += static_cast<size_t>(read_u64()); // dir_name_data
        pos += static_cast<size_t>(read_u64()); // file_name_data

        constexpr size_t kEntryRecordSize = 2 * sizeof(uint64_t) + 176;
        constexpr size_t kFileOrigSizeInRecord = 176;
        const uint64_t dir_count = read_u64();
        pos += static_cast<size_t>(dir_count) * (sizeof(uint64_t) + kEntryRecordSize);
        const uint64_t file_count = read_u64();
        ASSERT_GE(file_count, 1U);
        const size_t first_record_offset = pos + sizeof(uint64_t); // skip the map key
        ASSERT_LT(first_record_offset + kEntryRecordSize, metadata_end);

        // Make the entry index claim a different original size than the block metadata.
        WriteU64At(bytes, first_record_offset + kFileOrigSizeInRecord, 0xDEADBEEFULL);

        const std::string metadata = bytes.substr(metadata_offset, metadata_size);
        WriteU64At(bytes, kMetadataChecksumField, XXH64(metadata.data(), metadata.size(), kHashSeed));

        const Status status = fixture.Unpack(bytes);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
        EXPECT_NE(status.error().err_message.find("disagrees with the entry index"), std::string::npos)
            << status.error().message();

        fixture.Cleanup();
    }

    TEST(MalformedArchive, RejectsOverlappingAndDuplicateBlocks)
    {
        MalformedArchiveFixture fixture;
        fixture.Create();
        const std::string good = fixture.Bytes();

        constexpr size_t kMetadataOffsetField = 96;
        constexpr size_t kMetadataSizeField = 112;
        constexpr size_t kMetadataTypeField = 120;
        constexpr size_t kMetadataChecksumField = 128;

        const size_t metadata_offset = ReadU64At(good, kMetadataOffsetField);
        const size_t metadata_size = ReadU64At(good, kMetadataSizeField);
        ASSERT_EQ(ReadU64At(good, kMetadataTypeField), static_cast<uint64_t>(CompressionType::kNone));

        size_t pos = metadata_offset;
        const size_t metadata_end = metadata_offset + metadata_size;
        const auto read_u64 = [&]()
        {
            EXPECT_LE(pos + sizeof(uint64_t), metadata_end);
            const uint64_t value = ReadU64At(good, pos);
            pos += sizeof(uint64_t);
            return value;
        };

        // PackFileMeta: four scalar fields, two name buffers, two entry maps, three
        // id -> vector maps.
        for (int i = 0; i < 4; ++i)
        {
            (void)read_u64();
        }
        pos += static_cast<size_t>(read_u64());
        pos += static_cast<size_t>(read_u64());

        constexpr size_t kEntryRecordSize = 2 * sizeof(uint64_t) + 176;
        size_t file_records_start = 0;
        uint64_t file_record_count = 0;
        for (int map = 0; map < 2; ++map)
        {
            if (map == 1)
            {
                file_records_start = pos;
            }
            const uint64_t count = read_u64();
            if (map == 1)
            {
                file_record_count = count;
            }
            pos += static_cast<size_t>(count) * (sizeof(uint64_t) + kEntryRecordSize);
        }
        for (int map = 0; map < 3; ++map)
        {
            const uint64_t count = read_u64();
            for (uint64_t entry = 0; entry < count; ++entry)
            {
                (void)read_u64(); // key
                const uint64_t value_count = read_u64();
                pos += static_cast<size_t>(value_count) * sizeof(uint64_t);
            }
        }

        const uint64_t block_count = read_u64();
        ASSERT_GE(block_count, 2U);

        const auto skip_block_meta = [&]()
        {
            for (int field = 0; field < 6; ++field)
            {
                (void)read_u64();
            }
            const uint64_t file_count = read_u64();
            pos += static_cast<size_t>(file_count) * sizeof(uint64_t);
            for (int vec = 0; vec < 2; ++vec) // orig sizes, compressed sizes
            {
                const uint64_t count = read_u64();
                pos += static_cast<size_t>(count) * sizeof(uint64_t);
            }
            const uint64_t type_count = read_u64();
            pos += static_cast<size_t>(type_count);
            for (int vec = 0; vec < 2; ++vec) // offsets, hashes
            {
                const uint64_t count = read_u64();
                pos += static_cast<size_t>(count) * sizeof(uint64_t);
            }
        };

        const size_t first_block_start = pos;
        const uint64_t first_block_id = ReadU64At(good, first_block_start);
        const uint64_t first_block_offset = ReadU64At(good, first_block_start + 16);
        const uint64_t first_block_size = ReadU64At(good, first_block_start + 32);
        ASSERT_GT(first_block_size, 0U);
        skip_block_meta();
        const size_t second_block_start = pos;
        ASSERT_LT(second_block_start, metadata_end);

        const auto with_metadata_checksum = [&](std::string& patched)
        {
            const std::string metadata = patched.substr(metadata_offset, metadata_size);
            WriteU64At(patched, kMetadataChecksumField, XXH64(metadata.data(), metadata.size(), kHashSeed));
        };

        {
            std::string patched = good;
            // Point the second block at the first block's start so their ranges overlap.
            WriteU64At(patched, second_block_start + 16, first_block_offset);
            with_metadata_checksum(patched);
            const Status status = fixture.Unpack(patched);
            ASSERT_FALSE(status);
            EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
            EXPECT_NE(status.error().err_message.find("data blocks overlap"), std::string::npos)
                << status.error().message();
        }
        {
            std::string patched = good;
            WriteU64At(patched, second_block_start, first_block_id);
            with_metadata_checksum(patched);
            const Status status = fixture.Unpack(patched);
            ASSERT_FALSE(status);
            EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
            EXPECT_NE(status.error().err_message.find("duplicate data block id"), std::string::npos)
                << status.error().message();
        }
        {
            std::string patched = good;
            // Make the second block reference the first block's file id.
            const uint64_t first_file_id = ReadU64At(good, first_block_start + sizeof(uint64_t) * 7);
            WriteU64At(patched, second_block_start + sizeof(uint64_t) * 7, first_file_id);
            with_metadata_checksum(patched);
            const Status status = fixture.Unpack(patched);
            ASSERT_FALSE(status);
            EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
            EXPECT_NE(status.error().err_message.find("disagrees with the entry index"), std::string::npos)
                << status.error().message();
        }
        {
            std::string patched = good;
            // Two distinct ids sharing one directory and one file name resolve to the
            // same output path.
            ASSERT_GE(file_record_count, 2U);
            const size_t first_record = file_records_start + sizeof(uint64_t) * 2; // count + key
            const size_t second_record = first_record + sizeof(uint64_t) + kEntryRecordSize;
            const std::string first_name_handle = good.substr(first_record, sizeof(uint64_t) * 2);
            patched.replace(second_record, sizeof(uint64_t) * 2, first_name_handle);
            const uint64_t first_dir_id = ReadU64At(good, first_block_start + sizeof(uint64_t));
            WriteU64At(patched, second_block_start + sizeof(uint64_t), first_dir_id);
            with_metadata_checksum(patched);
            const Status status = fixture.Unpack(patched);
            ASSERT_FALSE(status);
            EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
            EXPECT_NE(status.error().err_message.find("duplicate file name"), std::string::npos)
                << status.error().message();
        }

        fixture.Cleanup();
    }

    TEST(UnpackBudget, RejectsFileAboveDecompressionBudget)
    {
        MalformedArchiveFixture fixture;
        fixture.Create();
        BlockStore store;
        store.SetMaxDecompressedFileSize(1);
        const Status status = store.UnPackPipeline(fixture.archive.string(), fixture.out_dir.string(), true);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().type(), ErrorType::kCorruption);
        EXPECT_NE(status.error().err_message.find("decompression budget"), std::string::npos)
            << status.error().message();
        fixture.Cleanup();
    }


    namespace
    {
        struct UnpackPolicyFixture
        {
            fs::path root{"unpack_policy_test"};
            fs::path source_dir{root / "source"};
            fs::path archive{root / "source.ctar"};
            fs::path out_dir{root / "out"};

            void Create()
            {
                fs::remove_all(root);
                fs::create_directories(source_dir);
                std::ofstream(source_dir / "a.txt") << "hello";
                BlockStore store;
                ASSERT_TRUE(store.Scan(source_dir.string()));
                ASSERT_TRUE(store.PackPipeline(archive.string()));
            }

            [[nodiscard]] fs::path ExtractedFile() const
            {
                // The archive stores the path that was passed to Scan, relative to out_dir.
                return out_dir / source_dir / "a.txt";
            }

            void Cleanup() const { fs::remove_all(root); }
        };
    } // namespace

    TEST(UnpackPolicy, RefusesNonEmptyOutputWithoutForce)
    {
        UnpackPolicyFixture fixture;
        fixture.Create();
        fs::create_directories(fixture.out_dir);
        std::ofstream(fixture.out_dir / "mine.txt") << "keep me";

        {
            BlockStore store;
            const Status status = store.UnPackPipeline(fixture.archive.string(), fixture.out_dir.string());
            EXPECT_FALSE(status);
            if (!status)
            {
                EXPECT_NE(status.error().message().find("not empty"), std::string::npos);
            }
        }
        EXPECT_TRUE(fs::exists(fixture.out_dir / "mine.txt"));
        EXPECT_FALSE(fs::exists(fixture.ExtractedFile()));

        // With --force the same destination is allowed, and a conflicting file is replaced.
        fs::create_directories(fixture.ExtractedFile().parent_path());
        std::ofstream(fixture.ExtractedFile()) << "old";
        {
            BlockStore store;
            ASSERT_TRUE(store.UnPackPipeline(fixture.archive.string(), fixture.out_dir.string(), /*force=*/true));
        }
        EXPECT_TRUE(fs::exists(fixture.out_dir / "mine.txt"));
        EXPECT_EQ(ReadWholeFile(fixture.ExtractedFile().string()), "hello");
        fixture.Cleanup();
    }

    TEST(UnpackPolicy, AllowsEmptyOutputWithoutForce)
    {
        UnpackPolicyFixture fixture;
        fixture.Create();
        // Only empty directories: they do not count as content and must survive.
        fs::create_directories(fixture.out_dir / "pre_existing_empty_dir");

        {
            BlockStore store;
            ASSERT_TRUE(store.UnPackPipeline(fixture.archive.string(), fixture.out_dir.string()));
        }
        EXPECT_TRUE(fs::is_directory(fixture.out_dir / "pre_existing_empty_dir"));
        EXPECT_EQ(ReadWholeFile(fixture.ExtractedFile().string()), "hello");
        fixture.Cleanup();
    }

    TEST(UnpackPolicy, CleansUpAfterFailure)
    {
        UnpackPolicyFixture fixture;
        fixture.Create();

        // Corrupt the first payload byte so the per-file hash check fails.
        std::string bytes = ReadWholeFile(fixture.archive.string());
        const size_t payload_offset = ReadU64At(bytes, 64);
        ASSERT_LT(payload_offset, bytes.size());
        bytes[payload_offset] = static_cast<char>(bytes[payload_offset] ^ 0xFF);
        WriteWholeFile(fixture.archive.string(), bytes);

        // (a) destination absent: stays absent
        {
            BlockStore store;
            EXPECT_FALSE(store.UnPackPipeline(fixture.archive.string(), fixture.out_dir.string()));
        }
        EXPECT_FALSE(fs::exists(fixture.out_dir));

        // (b) destination exists with an empty directory: the directory survives,
        //     nothing else is left behind
        fs::create_directories(fixture.out_dir / "pre_existing_empty_dir");
        {
            BlockStore store;
            EXPECT_FALSE(store.UnPackPipeline(fixture.archive.string(), fixture.out_dir.string()));
        }
        EXPECT_TRUE(fs::is_directory(fixture.out_dir / "pre_existing_empty_dir"));
        EXPECT_FALSE(fs::exists(fixture.out_dir / fixture.source_dir));
        EXPECT_FALSE(fs::exists(fixture.ExtractedFile()));
        fixture.Cleanup();
    }

    TEST(PackPolicy, RefusesToOverwriteArchiveWithoutForce)
    {
        UnpackPolicyFixture fixture;
        fs::remove_all(fixture.root);
        fs::create_directories(fixture.source_dir);
        std::ofstream(fixture.source_dir / "a.txt") << "hello";
        std::ofstream(fixture.archive) << "old archive";

        {
            BlockStore store;
            ASSERT_TRUE(store.Scan(fixture.source_dir.string()));
            const Status status = store.PackPipeline(fixture.archive.string());
            EXPECT_FALSE(status);
            if (!status)
            {
                EXPECT_NE(status.error().message().find("already exists"), std::string::npos);
            }
        }
        EXPECT_EQ(ReadWholeFile(fixture.archive.string()), "old archive");

        {
            BlockStore store;
            ASSERT_TRUE(store.Scan(fixture.source_dir.string()));
            ASSERT_TRUE(store.PackPipeline(fixture.archive.string(), /*force=*/true));
        }
        EXPECT_GT(ReadWholeFile(fixture.archive.string()).size(), 16U);
        fixture.Cleanup();
    }


    TEST(PackLayout, BlocksAreNotPadded)
    {
        const fs::path root("pack_layout_test");
        const fs::path tree = root / "tree";
        fs::remove_all(root);
        for (int i = 0; i < 200; ++i)
        {
            const fs::path dir = tree / ("d" + std::to_string(i));
            fs::create_directories(dir);
            std::ofstream(dir / "f.txt") << "x";
        }
        const fs::path archive = root / "tree.ctar";
        {
            BlockStore store;
            ASSERT_TRUE(store.Scan(tree.string()));
            ASSERT_TRUE(store.PackPipeline(archive.string()));
        }
        // 200 blocks with a 1-byte payload. Padding every block to 4 KiB would add
        // ~800 KiB; the metadata itself is only ~124 KiB.
        EXPECT_LT(fs::file_size(archive), 200 * 1024);
        fs::remove_all(root);
    }

    TEST(PackLayout, CompressesMetadataAboveThreshold)
    {
        const fs::path root("metadata_compression_test");
        const fs::path tree = root / "tree";
        fs::remove_all(root);
        fs::create_directories(tree);
        for (int i = 0; i < 5000; ++i)
        {
            std::ofstream(tree / ("file_" + std::to_string(i) + ".txt")) << "x";
        }
        const fs::path archive = root / "tree.ctar";
        {
            BlockStore store;
            ASSERT_TRUE(store.Scan(tree.string()));
            ASSERT_TRUE(store.PackPipeline(archive.string()));
        }
        {
            BlockStore reader;
            ASSERT_TRUE(reader.ReadPackHeaderOnly(archive.string()));
            const auto& header = reader.GetPackHeader();
            EXPECT_GT(header.metadata_original_size, kMetadataCompressionThreshold);
            EXPECT_EQ(header.metadata_compressed_type, static_cast<uint64_t>(CompressionType::kLZ4));
            EXPECT_LT(header.metadata_compressed_size, header.metadata_original_size);
        }
        const fs::path out = root / "out";
        {
            BlockStore reader;
            ASSERT_TRUE(reader.UnPackPipeline(archive.string(), out.string()));
        }
        EXPECT_TRUE(fs::exists(out / tree / "file_0.txt"));
        EXPECT_TRUE(fs::exists(out / tree / "file_4999.txt"));
        fs::remove_all(root);
    }

    TEST(PackMetadata, PlanUsesCompressedOnlyWhenSmaller)
    {
        constexpr uint64_t kRaw = 4 * 1024 * 1024;
        constexpr uint64_t kLz4 = static_cast<uint64_t>(CompressionType::kLZ4);
        constexpr uint64_t kNone = static_cast<uint64_t>(CompressionType::kNone);

        const auto compressed = PlanMetadataStorage(kRaw, kRaw - 1, kLz4);
        EXPECT_TRUE(compressed.use_compressed);
        EXPECT_EQ(compressed.stored_size, kRaw - 1);
        EXPECT_EQ(compressed.header_size, kRaw - 1);
        EXPECT_EQ(compressed.header_type, kLz4);

        // Compression failure (0) and no-shrink results fall back to the raw bytes.
        for (const uint64_t size : {uint64_t{0}, kRaw, kRaw + 1})
        {
            const auto fallback = PlanMetadataStorage(kRaw, size, kLz4);
            EXPECT_FALSE(fallback.use_compressed);
            EXPECT_EQ(fallback.stored_size, kRaw);
            EXPECT_EQ(fallback.header_size, kRaw);
            EXPECT_EQ(fallback.header_type, kNone);
        }
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
