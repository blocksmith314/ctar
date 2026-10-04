#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "dir_compare.h"


namespace ctar::test
{
    static bool AreFilesEqual(const std::filesystem::path& file1, const std::filesystem::path& file2)
    {
        try
        {
            if (!std::filesystem::is_regular_file(file1) || !std::filesystem::is_regular_file(file2))
            {
                return false;
            }
            const auto sz1 = std::filesystem::file_size(file1);
            const auto sz2 = std::filesystem::file_size(file2);
            if (sz1 != sz2)
            {
                std::println(std::cerr, "File size mismatch: {} ({} bytes) vs {} ({} bytes)", file1.string(), sz1,
                             file2.string(), sz2);
                return false;
            }
            if (sz1 == 0)
            {
                return true;
            }
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return false;
        }

        std::ifstream f1(file1, std::ios::binary);
        std::ifstream f2(file2, std::ios::binary);
        if (!f1.is_open() || !f2.is_open())
        {
            std::println(std::cerr, "Failed to open file: {} or {}", file1.string(), file2.string());
            return false;
        }

        constexpr size_t buffer_size = 4096;
        std::vector<char> buf1(buffer_size);
        std::vector<char> buf2(buffer_size);

        std::streamsize read1, read2;
        do
        {
            f1.read(buf1.data(), static_cast<std::streamsize>(buffer_size));
            f2.read(buf2.data(), static_cast<std::streamsize>(buffer_size));
            read1 = f1.gcount();
            read2 = f2.gcount();

            if (read1 != read2)
            {
                std::println(std::cerr, "Read length mismatch: {} vs {}", read1, read2);
                return false;
            }

            if (std::memcmp(buf1.data(), buf2.data(), static_cast<size_t>(read1)) != 0)
            {
                std::println(std::cerr, "File content differ: {} <-> {}", file1.string(), file2.string());
                return false;
            }
        }
        while (read1 == static_cast<std::streamsize>(buffer_size));

        return true;
    }

    bool AreDirectoriesEqual(const std::filesystem::path& dir1, const std::filesystem::path& dir2)
    {
        try
        {
            if (!std::filesystem::exists(dir1) || !std::filesystem::exists(dir2))
            {
                std::println(std::cerr, "Directory not exist: {} or {}", dir1.string(), dir2.string());
                return false;
            }

            if (!std::filesystem::is_directory(dir1) || !std::filesystem::is_directory(dir2))
            {
                return false;
            }

            std::vector<std::filesystem::path> entries1;
            std::vector<std::filesystem::path> entries2;

            for (const auto& entry : std::filesystem::directory_iterator(dir1))
            {
                auto filename = entry.path().filename();
                if (filename == ".DS_Store")
                    continue;
                entries1.push_back(entry.path());
            }
            for (const auto& entry : std::filesystem::directory_iterator(dir2))
            {
                auto filename = entry.path().filename();
                if (filename == ".DS_Store")
                    continue;
                entries2.push_back(entry.path());
            }

            if (entries1.size() != entries2.size())
            {
                std::println(std::cerr, "Directory entry count mismatch: {} has {} items, {} has {} items",
                             dir1.string(), entries1.size(), dir2.string(), entries2.size());
                return false;
            }

            auto sort_by_filename = [](const std::filesystem::path& a, const std::filesystem::path& b)
            { return a.filename() < b.filename(); };
            std::sort(entries1.begin(), entries1.end(), sort_by_filename);
            std::sort(entries2.begin(), entries2.end(), sort_by_filename);

            for (size_t i = 0; i < entries1.size(); ++i)
            {
                const auto& entry1 = entries1[i];
                const auto& entry2 = entries2[i];

                if (entry1.filename() != entry2.filename())
                {
                    std::println(std::cerr, "Filename mismatch: {} vs {}", entry1.filename().string(),
                                 entry2.filename().string());
                    return false;
                }

                bool is_dir1 = std::filesystem::is_directory(entry1);
                bool is_dir2 = std::filesystem::is_directory(entry2);

                if (is_dir1 != is_dir2)
                {
                    std::println(std::cerr, "Type mismatch, one directory one file: {}", entry1.filename().string());
                    return false;
                }

                if (is_dir1)
                {
                    if (!AreDirectoriesEqual(entry1, entry2))
                    {
                        return false;
                    }
                }
                else
                {
                    if (!AreFilesEqual(entry1, entry2))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return false;
        }
    }
} // namespace ctar::test
