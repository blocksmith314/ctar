#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include "gtest/gtest.h"
#include "src/utils/serialize_helper.h"
#include "src/utils/status.h"
#include "test/tools/file_generator.h"

namespace ctar
{
    namespace fs = std::filesystem;
    fs::path test_root_path{"file_generator_test"};
    class FileGeneratorTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (fs::exists(test_root_path))
            {
                fs::remove_all(test_root_path);
            }
            fs::create_directories(test_root_path);
        }
        void TearDown() override { fs::remove_all(test_root_path); }

        struct DirMeta
        {
            size_t total_file_cnt;
            size_t min_file_size;
            size_t max_file_size;
        };
        static ResultStatus<DirMeta> GetDirMetaOfPath(const fs::path& dir)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
            {
                return error_io("{}", ec.message());
            }

            std::size_t count = 0;
            size_t min_file_size = std::numeric_limits<size_t>::max();
            size_t max_file_size = 0;

            for (const auto& entry : fs::recursive_directory_iterator(dir, ec))
            {
                if (ec)
                {
                    return error_io("{}", ec.message());
                }
                if (entry.is_regular_file(ec))
                {
                    if (ec)
                    {
                        return error_io("{}", ec.message());
                    }
                    const auto file_size = entry.file_size(ec);
                    if (ec)
                    {
                        return error_io("{}", ec.message());
                    }
                    min_file_size = std::min(min_file_size, static_cast<size_t>(file_size));
                    max_file_size = std::max(max_file_size, static_cast<size_t>(file_size));
                    ++count;
                }
            }

            if (count == 0)
            {
                min_file_size = 0;
                max_file_size = 0;
            }

            return DirMeta{count, min_file_size, max_file_size};
        }
    };

    TEST_F(FileGeneratorTest, GeneratePresetFiles)
    {
        test::FileGenerator gen(test_root_path);
        size_t bytes_per_file = 256;
        size_t bytes_per_line = 32;
        const auto st = gen.GeneratePresetFiles(bytes_per_file, bytes_per_line);
        ASSERT_TRUE(st);
        EXPECT_TRUE(gen.CheckPresetFilesReady());
        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_EQ(dir_meta.total_file_cnt, test::FileGenerator::GetPresetFiles().size());
        EXPECT_GE(dir_meta.min_file_size, bytes_per_file);
        EXPECT_GE(dir_meta.max_file_size, bytes_per_file);
    }

    TEST_F(FileGeneratorTest, GenerateRandomFilesWithFirstLevelDir)
    {
        test::FileGenerator gen(test_root_path);
        size_t file_count = 100;
        size_t min_file_size = 1024;
        size_t max_file_size = 2048;
        auto st = gen.GenerateRandomFiles(3, 2, file_count, min_file_size, max_file_size, false);
        ASSERT_TRUE(st);
        EXPECT_TRUE(fs::exists(test_root_path / "level1_0"));
        EXPECT_TRUE(fs::exists(test_root_path / "level1_1"));
        EXPECT_TRUE(fs::exists(test_root_path / "level1_2"));
        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_EQ(dir_meta.total_file_cnt, file_count);
        EXPECT_GE(dir_meta.min_file_size, min_file_size);
        EXPECT_LE(dir_meta.max_file_size, max_file_size);
    }

    TEST_F(FileGeneratorTest, GenerateCompressionFriendlyFiles)
    {
        test::FileGenerator gen(test_root_path);
        size_t file_count = 3;
        size_t min_file_size = 4096;
        size_t max_file_size = 4096;
        auto st = gen.GenerateRandomFiles(1, 1, 3, 4096, 4096, false, test::CompressLevel::HighCompress);
        ASSERT_TRUE(st);

        std::error_code ec;
        bool found = false;
        for (const auto& entry : fs::recursive_directory_iterator(test_root_path, ec))
        {
            ASSERT_FALSE(ec) << "Iterate dir error: " << ec.message();
            if (!entry.is_regular_file(ec))
                continue;

            auto p = entry.path();
            if (p.filename().string().starts_with("rand_file_"))
            {
                found = true;
                std::ifstream input(p, std::ios::binary);
                ASSERT_TRUE(input) << "Cannot open " << p;

                std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                ASSERT_EQ(content.size(), 4096U);
                EXPECT_GT(std::count(content.begin(), content.end(), 'A'), 4000);
                break;
            }
        }
        ASSERT_TRUE(found) << "No rand_file found";

        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_EQ(dir_meta.total_file_cnt, file_count);
        EXPECT_GE(dir_meta.min_file_size, min_file_size);
        EXPECT_LE(dir_meta.max_file_size, max_file_size);
    }


    TEST_F(FileGeneratorTest, NoFirstLevelFolder)
    {
        test::FileGenerator gen(test_root_path);
        auto st = gen.GenerateRandomFiles(0, 1, 50, 512, 1024, false);
        ASSERT_TRUE(st);
        int file_count = 0;
        for (auto& entry : fs::directory_iterator(test_root_path))
        {
            if (entry.is_regular_file())
                file_count++;
        }
        EXPECT_GE(file_count, 1);
    }

    TEST_F(FileGeneratorTest, JunkFileOnlyCreatedOnce)
    {
        test::FileGenerator gen(test_root_path);
        auto st = gen.GenerateRandomFiles(2, 1, 20, 512, 1024, true);
        ASSERT_TRUE(st);
        std::error_code ec;
        size_t junk_count_before = 0;
        for (const auto& entry : fs::directory_iterator(test_root_path, ec))
        {
            ASSERT_FALSE(ec);
            if (entry.is_regular_file(ec))
            {
                for (const auto& junk_file : ctar::junk_files)
                {
                    if (junk_file == entry.path().filename())
                    {
                        junk_count_before++;
                    }
                }
            }
        }
        ASSERT_GT(junk_count_before, 0U) << "Junk files were not created";
    }

    TEST_F(FileGeneratorTest, Batch)
    {
        test::FileGenerator gen(test_root_path);
        size_t file_count = 10000;
        size_t min_file_size = 512;
        size_t max_file_size = 1024000;
        auto st = gen.GenerateRandomFiles(2, 1, file_count, min_file_size, max_file_size, true);
        ASSERT_TRUE(st);
        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_GE(dir_meta.total_file_cnt, file_count);
        EXPECT_GE(dir_meta.min_file_size, min_file_size);
        EXPECT_LE(dir_meta.max_file_size, max_file_size);
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}