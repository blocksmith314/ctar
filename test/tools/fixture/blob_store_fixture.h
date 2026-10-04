#ifndef BLOB_STORE_FIXTURE_H
#define BLOB_STORE_FIXTURE_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include "test/tools/file_generator.h"

namespace fs = std::filesystem;

namespace ctar::test
{
    class PresetFileFixture : public ::testing::Test
    {
    public:
        size_t bytes_per_file_{256};
        size_t bytes_per_line_{32};

        fs::path source_dir_{"preset_file_test_dir"};
        fs::path pack_dir_{"preset_target_test_dir"};
        std::string pack_file = (pack_dir_ / "preset_file.ctar").string();
        fs::path unpack_dir_{"preset_unpack_test_dir"};
        std::vector<std::string> preset_key_words;
        std::vector<std::string> preset_dirs;
        std::vector<std::string> file_paths;

        size_t generator_file_size = 0;
        size_t generator_file_cnt = 0;

    protected:
        void SetUp() override
        {
            CleanAllDirectories();
            fs::create_directories(source_dir_);
            fs::create_directories(pack_dir_);
            fs::create_directories(unpack_dir_);

            FileGenerator generator_preset(source_dir_);
            if (const auto st = generator_preset.GeneratePresetFiles(bytes_per_file_, bytes_per_line_); !st)
            {
                throw std::runtime_error("fail to generate preset files");
            }
            auto preset_files = FileGenerator::GetPresetFiles();
            preset_key_words.clear();
            file_paths.clear();
            for (const auto& preset_file : preset_files)
            {
                file_paths.emplace_back(source_dir_ / preset_file);
                auto components = split_path_components(preset_file);
                preset_key_words.insert(preset_key_words.end(), components.begin(), components.end());
            }
            preset_dirs = generator_preset.GetPresetDirs();
            generator_file_size = generator_preset.total_file_size;
            generator_file_cnt = generator_preset.file_cnt;
        }

        void TearDown() override
        {
            // CleanAllDirectories();
        }

        void CleanAllDirectories() const
        {
            if (fs::exists(source_dir_))
                fs::remove_all(source_dir_);
            if (fs::exists(pack_dir_))
                fs::remove_all(pack_dir_);
            if (fs::exists(unpack_dir_))
                fs::remove_all(unpack_dir_);
        }

        static std::vector<std::string> split_path_components(const std::string& path)
        {
            std::vector<std::string> parts;
            std::string temp;
            for (const char ch : path)
            {
                if (ch == '/' || ch == '\\')
                {
                    if (!temp.empty())
                    {
                        parts.push_back(temp);
                        temp.clear();
                    }
                }
                else
                {
                    temp.push_back(ch);
                }
            }
            if (!temp.empty())
            {
                parts.push_back(temp);
            }
            return parts;
        }
    };
}

#endif //BLOB_STORE_FIXTURE_H