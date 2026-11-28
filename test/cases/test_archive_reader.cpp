#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ctar/reader.h>

#include "src/core/block_store.h"

namespace ctar
{

    namespace
    {
        struct ArchiveReaderFixture
        {
            fs::path root{"archive_reader_test"};
            fs::path source{root / "source"};
            fs::path archive{root / "sample.ctar"};

            void Create()
            {
                fs::remove_all(root);
                fs::create_directories(source / "sub" / "deep");
                std::ofstream(source / "hello.txt") << "hello world\n";
                std::ofstream empty_file(source / "empty.bin");
                // Listed in kNoCompressSuffixes, so it is stored raw.
                std::ofstream(source / "sample.parquet") << "PAR1-not-really";
                std::ofstream(source / "sub" / "nested.txt") << "nested";
                std::ofstream(source / "sub" / "deep" / "leaf.bin") << std::string(4096, 'x');

                BlockStore store;
                ASSERT_TRUE(store.Scan(source.string()));
                ASSERT_TRUE(store.PackPipeline(archive.string()));
            }

            void Cleanup() const { fs::remove_all(root); }

            [[nodiscard]] ArchiveReader OpenReader(ReaderOptions options = {}) const
            {
                auto reader = ArchiveReader::Open(archive.string(), options);
                EXPECT_TRUE(reader) << reader.error().message;
                return reader ? std::move(reader.value()) : ArchiveReader{};
            }
        };

        std::vector<std::string> EntryPaths(const std::vector<Entry>& entries)
        {
            std::vector<std::string> paths;
            paths.reserve(entries.size());
            for (const auto& entry : entries)
            {
                paths.push_back(entry.path);
            }
            return paths;
        }

        const Entry* FindEntry(const std::vector<Entry>& entries, const std::string_view path)
        {
            for (const auto& entry : entries)
            {
                if (entry.path == path)
                {
                    return &entry;
                }
            }
            return nullptr;
        }
    } // namespace

    TEST(ArchiveReader, ListRootDirectChildren)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto entries = reader.List("/");
        ASSERT_TRUE(entries) << entries.error().message;
        EXPECT_EQ(EntryPaths(entries.value()),
                  (std::vector<std::string>{"/empty.bin", "/hello.txt", "/sample.parquet", "/sub"}));

        const Entry* hello = FindEntry(entries.value(), "/hello.txt");
        ASSERT_NE(hello, nullptr);
        EXPECT_EQ(hello->kind, EntryKind::kFile);
        EXPECT_EQ(hello->size, 12U);

        const Entry* sub = FindEntry(entries.value(), "/sub");
        ASSERT_NE(sub, nullptr);
        EXPECT_EQ(sub->kind, EntryKind::kDirectory);
        EXPECT_EQ(sub->size, 0U);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, ListSubdirectory)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto entries = reader.List("/sub");
        ASSERT_TRUE(entries) << entries.error().message;
        EXPECT_EQ(EntryPaths(entries.value()), (std::vector<std::string>{"/sub/deep", "/sub/nested.txt"}));

        fixture.Cleanup();
    }

    TEST(ArchiveReader, ListRecursive)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto entries = reader.List("/", true);
        ASSERT_TRUE(entries) << entries.error().message;
        EXPECT_EQ(EntryPaths(entries.value()),
                  (std::vector<std::string>{"/empty.bin", "/hello.txt", "/sample.parquet", "/sub", "/sub/deep",
                                            "/sub/deep/leaf.bin", "/sub/nested.txt"}));

        fixture.Cleanup();
    }

    TEST(ArchiveReader, StatFileAndDirectory)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto file = reader.Stat("/hello.txt");
        ASSERT_TRUE(file) << file.error().message;
        EXPECT_EQ(file->kind, EntryKind::kFile);
        EXPECT_EQ(file->size, 12U);
        EXPECT_GT(file->mtime_ms, 0);

        auto dir = reader.Stat("/sub");
        ASSERT_TRUE(dir) << dir.error().message;
        EXPECT_EQ(dir->kind, EntryKind::kDirectory);

        auto root = reader.Stat("/");
        ASSERT_TRUE(root) << root.error().message;
        EXPECT_EQ(root->kind, EntryKind::kDirectory);
        EXPECT_EQ(root->path, "/");

        // A path without a leading slash is accepted as root-relative.
        auto relative = reader.Stat("hello.txt");
        ASSERT_TRUE(relative) << relative.error().message;
        EXPECT_EQ(relative->path, "/hello.txt");

        fixture.Cleanup();
    }

    TEST(ArchiveReader, StatErrors)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto missing = reader.Stat("/does-not-exist");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error().code, ErrorCode::kNotFound);

        auto traversal = reader.Stat("/../etc/passwd");
        ASSERT_FALSE(traversal);
        EXPECT_EQ(traversal.error().code, ErrorCode::kInvalidArgument);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, ReadFileContents)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto hello = reader.ReadFile("/hello.txt");
        ASSERT_TRUE(hello) << hello.error().message;
        EXPECT_EQ(hello.value()->view(), "hello world\n");

        auto empty = reader.ReadFile("/empty.bin");
        ASSERT_TRUE(empty) << empty.error().message;
        EXPECT_EQ(empty.value()->size(), 0U);

        // Stored raw because of the suffix.
        auto parquet = reader.ReadFile("/sample.parquet");
        ASSERT_TRUE(parquet) << parquet.error().message;
        EXPECT_EQ(parquet.value()->view(), "PAR1-not-really");

        auto missing = reader.ReadFile("/nope");
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.error().code, ErrorCode::kNotFound);

        auto directory = reader.ReadFile("/sub");
        ASSERT_FALSE(directory);
        EXPECT_EQ(directory.error().code, ErrorCode::kInvalidArgument);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, OpenFileReadAt)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();
        auto reader = fixture.OpenReader();

        auto file = reader.OpenFile("/hello.txt");
        ASSERT_TRUE(file) << file.error().message;
        EXPECT_EQ(file->Size(), 12U);

        char buffer[5] = {};
        auto count = file->ReadAt(6, std::span<char>(buffer, sizeof(buffer)));
        ASSERT_TRUE(count) << count.error().message;
        EXPECT_EQ(count.value(), 5U);
        EXPECT_EQ(std::string_view(buffer, 5), "world");

        auto at_end = file->ReadAt(12, std::span<char>(buffer, sizeof(buffer)));
        ASSERT_TRUE(at_end) << at_end.error().message;
        EXPECT_EQ(at_end.value(), 0U);

        char sequential[8] = {};
        auto first = file->Read(std::span<char>(sequential, 5));
        ASSERT_TRUE(first) << first.error().message;
        EXPECT_EQ(std::string_view(sequential, first.value()), "hello");
        auto second = file->Read(std::span<char>(sequential, sizeof(sequential)));
        ASSERT_TRUE(second) << second.error().message;
        EXPECT_EQ(std::string_view(sequential, second.value()), " world\n");
        auto third = file->Read(std::span<char>(sequential, sizeof(sequential)));
        ASSERT_TRUE(third) << third.error().message;
        EXPECT_EQ(third.value(), 0U);

        auto past_end = file->ReadAt(13, std::span<char>(buffer, sizeof(buffer)));
        ASSERT_FALSE(past_end);
        EXPECT_EQ(past_end.error().code, ErrorCode::kInvalidArgument);

        EXPECT_EQ(file->ReadAll()->view(), "hello world\n");

        auto directory = reader.OpenFile("/sub");
        ASSERT_FALSE(directory);
        EXPECT_EQ(directory.error().code, ErrorCode::kInvalidArgument);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, FileOutlivesReader)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();

        std::optional<ArchiveReader> reader = fixture.OpenReader();
        auto file = reader->OpenFile("/hello.txt");
        ASSERT_TRUE(file) << file.error().message;
        reader.reset();

        EXPECT_EQ(file->ReadAll()->view(), "hello world\n");
        EXPECT_EQ(file->Size(), 12U);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, EnforcesFileBudgetPerRead)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();

        ReaderOptions options;
        options.max_file_size = 1;
        auto reader = fixture.OpenReader(options);
        // Opening still succeeds: the budget applies to individual reads.
        EXPECT_TRUE(reader.ReadFile("/empty.bin"));
        auto too_large = reader.ReadFile("/hello.txt");
        ASSERT_FALSE(too_large);
        EXPECT_EQ(too_large.error().code, ErrorCode::kTooLarge);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, EnforcesMetadataBudgetOnOpen)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();

        ReaderOptions options;
        options.max_metadata_size = 1;
        auto reader = ArchiveReader::Open(fixture.archive.string(), options);
        ASSERT_FALSE(reader);
        EXPECT_EQ(reader.error().code, ErrorCode::kCorruption);

        fixture.Cleanup();
    }

    TEST(ArchiveReader, MovedFromReaderIsUnusable)
    {
        ArchiveReaderFixture fixture;
        fixture.Create();

        ArchiveReader empty;
        auto status = empty.List("/");
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, ErrorCode::kInvalidArgument);

        auto reader = fixture.OpenReader();
        ArchiveReader moved = std::move(reader);
        auto entries = moved.List("/");
        ASSERT_TRUE(entries) << entries.error().message;
        EXPECT_FALSE(entries->empty());

        fixture.Cleanup();
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
