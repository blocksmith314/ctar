#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numeric>
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
        test::GeneratorOption option;
        option.file_count = 100;
        option.min_file_size = 1024;
        option.max_file_size = 2048;
        option.first_level_dir_count = 3;
        option.max_dir_depth = 2;
        option.create_junk_file = false;
        auto st = gen.GenerateRandomFiles(option);
        ASSERT_TRUE(st);
        EXPECT_TRUE(fs::exists(test_root_path / "level1_0"));
        EXPECT_TRUE(fs::exists(test_root_path / "level1_1"));
        EXPECT_TRUE(fs::exists(test_root_path / "level1_2"));
        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_EQ(dir_meta.total_file_cnt, option.file_count);
        EXPECT_GE(dir_meta.min_file_size, option.min_file_size);
        EXPECT_LE(dir_meta.max_file_size, option.max_file_size);
    }

    TEST_F(FileGeneratorTest, GenerateCompressionFriendlyFiles)
    {
        test::FileGenerator gen(test_root_path);
        test::GeneratorOption option;
        option.file_count = 3;
        option.min_file_size = 4096;
        option.max_file_size = 4096;
        option.first_level_dir_count = 1;
        option.max_dir_depth = 1;
        option.create_junk_file = false;
        option.compress_level = test::CompressLevel::HighCompress;
        auto st = gen.GenerateRandomFiles(option);
        ASSERT_TRUE(st);

        std::error_code ec;
        bool found = false;
        for (const auto& entry : fs::recursive_directory_iterator(test_root_path, ec))
        {
            ASSERT_FALSE(ec) << "Iterate dir error: " << ec.message();
            if (!entry.is_regular_file(ec))
                continue;

            if (const auto& p = entry.path(); p.filename().string().starts_with("rand_file_"))
            {
                found = true;
                std::ifstream input(p, std::ios::binary);
                ASSERT_TRUE(input) << "Cannot open " << p;
                std::stringstream ss;
                ss << input.rdbuf();
                std::string content = ss.str();
                ASSERT_EQ(content.size(), 4096U);
                EXPECT_GT(std::ranges::count(content.begin(), content.end(), 'A'), 4000);
                break;
            }
        }
        ASSERT_TRUE(found) << "No rand_file found";

        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_EQ(dir_meta.total_file_cnt, option.file_count);
        EXPECT_GE(dir_meta.min_file_size, option.min_file_size);
        EXPECT_LE(dir_meta.max_file_size, option.max_file_size);
    }


    TEST_F(FileGeneratorTest, NoFirstLevelFolder)
    {
        test::FileGenerator gen(test_root_path);
        test::GeneratorOption option;
        option.first_level_dir_count = 0;
        option.max_dir_depth = 1;
        option.file_count = 50;
        option.min_file_size = 512;
        option.max_file_size = 1024;
        option.create_junk_file = false;
        auto st = gen.GenerateRandomFiles(option);
        ASSERT_TRUE(st);
        int file_count = 0;
        for (auto& entry : fs::directory_iterator(test_root_path))
        {
            if (entry.is_regular_file())
                file_count++;
        }
        EXPECT_EQ(file_count, option.file_count);
    }

    TEST_F(FileGeneratorTest, JunkFileOnlyCreatedOnce)
    {
        test::FileGenerator gen(test_root_path);
        test::GeneratorOption option;
        option.first_level_dir_count = 2;
        option.max_dir_depth = 1;
        option.file_count = 20;
        option.min_file_size = 512;
        option.max_file_size = 1024;
        option.create_junk_file = true;
        auto st = gen.GenerateRandomFiles(option);
        ASSERT_TRUE(st);
        std::error_code ec;
        size_t junk_file_count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(test_root_path, ec))
        {
            ASSERT_FALSE(ec);
            if (entry.is_regular_file(ec))
            {
                for (const auto& junk_file : ctar::junk_files)
                {
                    if (junk_file == entry.path().filename())
                    {
                        junk_file_count++;
                    }
                }
            }
        }
        ASSERT_EQ(junk_file_count, junk_files.size()) << "Junk files were not created";
    }


    TEST_F(FileGeneratorTest, SpecialFile)
    {
        test::FileGenerator gen(test_root_path);
        test::GeneratorOption option;
        option.first_level_dir_count = 2;
        option.max_dir_depth = 1;
        option.file_count = 201;
        option.min_file_size = 20;
        option.max_file_size = 512;
        option.create_junk_file = false;
        option.create_zero_file = true;
        auto st = gen.GenerateRandomFiles(option);
        ASSERT_TRUE(st);
        std::error_code ec;
        size_t zero_size_file_cnt = 0;
        size_t tar_file_cnt = 0;
        for (const auto& entry : fs::recursive_directory_iterator(test_root_path, ec))
        {
            ASSERT_FALSE(ec);
            if (entry.is_regular_file(ec))
            {
                if (entry.file_size() == 0)
                {
                    zero_size_file_cnt++;
                }
                if (entry.path().filename().string().ends_with(test::kCompressedFileSuffix))
                {
                    tar_file_cnt++;
                }
            }
        }
        ASSERT_EQ(zero_size_file_cnt, (option.file_count + option.zero_file_interval - 1) / option.zero_file_interval)
            << "zero size files were not created";

        ASSERT_EQ(tar_file_cnt,
                  (option.file_count + option.compressed_file_interval - 1) / option.compressed_file_interval)
            << "compressed file were not created";
    }

    TEST_F(FileGeneratorTest, Batch)
    {
        test::FileGenerator gen(test_root_path);

        test::GeneratorOption option;
        option.first_level_dir_count = 2;
        option.max_dir_depth = 1;
        option.file_count = 10000;
        option.min_file_size = 512;
        option.max_file_size = 1024000;
        option.create_junk_file = true;
        auto st = gen.GenerateRandomFiles(option);
        ASSERT_TRUE(st);
        auto dir_meta_res = GetDirMetaOfPath(test_root_path);
        ASSERT_TRUE(dir_meta_res.has_value()) << "Count files error: " << dir_meta_res.error().message();
        auto dir_meta = dir_meta_res.value();
        EXPECT_GE(dir_meta.total_file_cnt, option.file_count);
        EXPECT_GE(dir_meta.min_file_size, option.min_file_size);
        EXPECT_LE(dir_meta.max_file_size, option.max_file_size);
    }

} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
