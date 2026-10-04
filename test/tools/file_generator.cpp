#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string_view>
#ifdef _WIN32
#else
#include <unistd.h>
#endif
#include <utility>
#include <print>

#include "file_generator.h"

#ifdef FILE_GENERATOR_STANDALONE
namespace
{
    namespace fs = std::filesystem;
    struct GeneratorOptions
    {
        fs::path output_dir = "./test_data";
        ctar::test::CompressLevel compress = ctar::test::CompressLevel::NoCompress;
        ctar::test::RandomSeedMode random_seed_mode = ctar::test::RandomSeedMode::AUTO;
        bool random_mode = true;
        bool create_junk = false;

        uint32_t first_level_dir_count = 2;
        uint32_t max_dir_depth = 3;
        uint32_t file_count = 100;

        // use for random_mode = false
        uint32_t bytes_per_file = 60;
        uint32_t bytes_per_line = 20;

        // use for random_mode = true
        uint64_t min_file_size = 1024;
        uint64_t max_file_size = 1024 * 1024;
    };

    void PrintUsage(const char* program_name)
    {
        constexpr std::string_view help_template = R"(Usage: {} [options]
          --dir <path>                 output directory (default: ./test_data)
          --mode random|preset         generation mode (default: random)
          --count <n>                  number of random files (default: 100)
          --first-level <n>            number of first-level dirs (default: 2)
          --depth <n>                  max directory tree depth (default: 3)
          --min-size <bytes>           minimum random file size (default: 1024)
          --max-size <bytes>           maximum random file size (default: 1048576)
          --junk                       create junk files in root directory
          --compress high|medium|none  file content entropy (default: none)
          --bytes-per-file <n>         preset file size (default: 60)
          --bytes-per-line <n>         preset bytes per line (default: 20)
          --random-seed fixed|auto   set the seed of random (default: fixed)
          -h, --help                   show help)";
        std::println(help_template, program_name);
    }

    bool ParseCompressLevel(const std::string_view text, ctar::test::CompressLevel& level)
    {
        if (text == "high")
        {
            level = ctar::test::CompressLevel::HighCompress;
            return true;
        }
        if (text == "medium")
        {
            level = ctar::test::CompressLevel::MediumCompress;
            return true;
        }
        if (text == "none")
        {
            level = ctar::test::CompressLevel::NoCompress;
            return true;
        }
        return false;
    }

    bool ParseRandomSeedMode(const std::string_view text, ctar::test::RandomSeedMode& seed_mode)
    {
        if (text == "auto")
        {
            seed_mode = ctar::test::RandomSeedMode::AUTO;
            return true;
        }
        if (text == "fixed")
        {
            seed_mode = ctar::test::RandomSeedMode::FIXED;
            return true;
        }
        return false;
    }

    bool ParseArgs(int argc, char** argv, GeneratorOptions& options)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            if (arg == "-h" || arg == "--help")
            {
                return false;
            }
            if (arg == "--dir")
            {
                if (i + 1 >= argc)
                {
                    return false;
                }
                options.output_dir = argv[++i];
            }
            else if (arg == "--mode")
            {
                if (i + 1 >= argc)
                {
                    return false;
                }
                const std::string_view mode = argv[++i];
                options.random_mode = mode == "random";
                if (mode != "random" && mode != "preset")
                {
                    std::println(std::cerr, "invalid mode: {}", mode);
                    return false;
                }
            }
            else if (arg == "--count")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.file_count))
                {
                    return false;
                }
                if (options.file_count == 0)
                {
                    std::println(std::cerr, "--count must be greater than 0");
                    return false;
                }
            }
            else if (arg == "--first-level")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.first_level_dir_count))
                {
                    return false;
                }
            }
            else if (arg == "--depth")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.max_dir_depth))
                {
                    return false;
                }
                if (options.max_dir_depth == 0)
                {
                    std::println(std::cerr, "--depth must be greater than 0");
                    return false;
                }
            }
            else if (arg == "--min-size")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.min_file_size))
                {
                    return false;
                }
            }
            else if (arg == "--max-size")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.max_file_size))
                {
                    return false;
                }
            }
            else if (arg == "--junk")
            {
                options.create_junk = true;
            }
            else if (arg == "--compress")
            {
                if (i + 1 >= argc || !ParseCompressLevel(argv[++i], options.compress))
                {
                    std::println(std::cerr, "--compress must be high, medium, or none");
                    return false;
                }
            }
            else if (arg == "--bytes-per-file")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.bytes_per_file))
                {
                    return false;
                }
            }
            else if (arg == "--bytes-per-line")
            {
                if (i + 1 >= argc || !ctar::test::ParseUnsigned(argv[++i], options.bytes_per_line))
                {
                    return false;
                }
            }
            else if (arg == "--random-seed")
            {
                if (i + 1 >= argc || !ParseRandomSeedMode(argv[++i], options.random_seed_mode))
                {
                    return false;
                }
            }
            else
            {
                std::println(std::cerr, "unknown argument: {}", arg);
                return false;
            }
        }
        if (options.min_file_size > options.max_file_size)
        {
            std::println(std::cerr, "--min-size must be <= --max-size");
            return false;
        }
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    GeneratorOptions options;
    if (argc > 1 && (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help"))
    {
        PrintUsage(argv[0]);
        return 0;
    }
    if (!ParseArgs(argc, argv, options))
    {
        PrintUsage(argv[0]);
        return 1;
    }

    try
    {
        fs::remove_all(options.output_dir);
        fs::create_directories(options.output_dir);

        ctar::test::FileGenerator generator(options.output_dir, options.random_seed_mode);
        bool res;

        if (options.random_mode)
        {
            res = generator.GenerateRandomFiles(options.first_level_dir_count, options.max_dir_depth,
                                                options.file_count, options.min_file_size, options.max_file_size,
                                                options.create_junk, options.compress);
        }
        else
        {
            res = generator.GeneratePresetFiles(options.bytes_per_file, options.bytes_per_line);
        }

        if (!res)
        {
            fs::remove_all(options.output_dir);
            return 1;
        }

        std::println("output_dir: {}, files: {}, total_size: {} bytes", options.output_dir.string(), generator.file_cnt,
                     generator.total_file_size);
        return 0;
    }
    catch (const std::exception& e)
    {
        fs::remove_all(options.output_dir);
        std::println(std::cerr, "fatal error: {}", e.what());
        return 1;
    }
}

#endif

namespace ctar::test
{
    namespace
    {
        uint64_t calc_digit_width(uint64_t count)
        {
            if (count == 0)
                return 1;
            uint64_t w = 1;
            uint64_t tmp = count - 1;
            while (tmp >= 10)
            {
                tmp /= 10;
                w++;
            }
            return w;
        }
    } // namespace

    void FileGenerator::BuildDirectoryPool(uint64_t first_level_dir_count, uint64_t max_dir_depth,
                                           std::vector<std::filesystem::path>& dir_pool) const
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> top_nodes;

        if (first_level_dir_count > 0)
        {
            for (uint64_t i = 0; i < first_level_dir_count; ++i)
            {
                auto top_dir = output_base_path_ / std::format("level1_{}", i);
                fs::create_directories(top_dir);
                top_nodes.emplace_back(std::move(top_dir));
            }
        }
        else
        {
            top_nodes.push_back(output_base_path_);
        }

        std::uniform_int_distribution<uint64_t> tree_depth_dist{1, max_dir_depth};
        std::uniform_int_distribution<uint64_t> sub_dir_cnt_dist{0, 2};

        for (auto& top : top_nodes)
        {
            const auto branch_max_depth = tree_depth_dist((*gen_));
            dir_pool.push_back(top);

            std::vector<fs::path> current_level;
            current_level.push_back(top);
            std::vector<fs::path> next_level;

            for (uint64_t sub_depth = 1; sub_depth < branch_max_depth; ++sub_depth)
            {
                next_level.clear();
                for (const auto& parent_dir : current_level)
                {
                    uint64_t sub_count = sub_dir_cnt_dist(*gen_);
                    for (uint64_t seq = 0; seq < sub_count; ++seq)
                    {
                        fs::path child = parent_dir / std::format("level{}_{}", sub_depth + 1, seq);
                        fs::create_directories(child);
                        dir_pool.push_back(child);
                        next_level.push_back(child);
                    }
                }

                if (next_level.empty() && !current_level.empty())
                {
                    const auto& parent_dir = current_level.front();
                    fs::path child = parent_dir / std::format("level{}_{}", sub_depth + 1, 0);
                    fs::create_directories(child);
                    dir_pool.push_back(child);
                    next_level.push_back(child);
                }
                current_level.swap(next_level);
            }
        }
    }


    FileGenerator::FileGenerator(fs::path base_path, const RandomSeedMode random_seed_mode) :
        output_base_path_(std::move(base_path))
    {
        if (random_seed_mode == RandomSeedMode::FIXED)
        {
            // Fixed seed for deterministic benchmark test data.
            // Predictable sequence is intentional for reproducible tests.
            constexpr uint64_t fixed_seed = 0x123456789ABCDEF0ULL;
            gen_.emplace(fixed_seed);
        }
        else
        {
            gen_.emplace(rd_());
        }
    }

    std::vector<std::string> FileGenerator::GetPresetDirs() const
    {
        std::unordered_set<std::string> dir_set;
        for (const auto& filepath : preset_file_names_)
        {
            fs::path p(filepath);
            if (auto dir_path = p.parent_path(); !dir_path.empty())
            {
                dir_set.insert((output_base_path_ / dir_path).string());
            }
        }
        return {dir_set.begin(), dir_set.end()};
    }

    bool FileGenerator::WriteRandomFile(const fs::path& file_path, const uint64_t file_size,
                                        const CompressLevel compress_level) const
    {
        if (!gen_.has_value())
        {
            std::println("random generator not initialized");
            return false;
        }

        auto fill_random_data = [this](char* buf, const size_t buf_len)
        {
            size_t j = 0;
            while (j < buf_len)
            {
                uint64_t v = (*gen_)();
                const size_t rem = std::min(sizeof(uint64_t), buf_len - j);
                std::memcpy(buf + j, &v, rem);
                j += rem;
            }
        };

        std::ofstream out(file_path, std::ios::binary);
        if (!out.is_open())
        {
            std::println("failed to open file {}", file_path.string());
            return false;
        }

        std::vector<char> chunk_buf(kChunkSize);
        uint64_t remain = file_size;

        auto fill_chunk = [&](char* buf, const uint64_t len)
        {
            switch (compress_level)
            {
            case CompressLevel::HighCompress:
                std::ranges::fill(buf, buf + len, 'A');
                for (uint64_t j = 0; j < len; j += 256)
                {
                    buf[j] = static_cast<char>((*gen_)() & 0xFFU);
                }
                break;
            case CompressLevel::MediumCompress:
                {
                    const uint64_t half = len / 2;
                    std::fill_n(buf, half, 'B');
                    fill_random_data(buf + half, len - half);
                    break;
                }
            case CompressLevel::NoCompress:
            default:
                fill_random_data(buf, len);
                break;
            }
        };

        while (remain > 0)
        {
            const uint64_t write_len = std::min(remain, kChunkSize);
            fill_chunk(chunk_buf.data(), write_len);

            out.write(chunk_buf.data(), static_cast<std::streamsize>(write_len));
            if (!out)
            {
                std::println("write failed at path {}, offset {}", file_path.string(), file_size - remain);
                return false;
            }
            remain -= write_len;
        }

        out.flush();
        if (!out)
        {
            std::println("final flush failed for file {}", file_path.string());
            return false;
        }
        out.close();
        if (!out)
        {
            std::println("close failed for file {}", file_path.string());
            return false;
        }
        return true;
    }


    bool FileGenerator::CreateJunkFiles(const fs::path& dir) const
    {
        for (const auto& file_name : junk_files)
        {
            if (auto res = WriteRandomFile(dir / file_name, kJunkFileSize, CompressLevel::NoCompress); !res)
            {
                return res;
            }
        }
        return true;
    }


    bool FileGenerator::GeneratePresetFiles(const uint64_t bytes_per_file, const uint64_t bytes_per_line)
    {
        uint64_t i = 0;
        total_file_size = 0;
        file_cnt = 0;
        for (const auto& file_name : preset_file_names_)
        {
            fs::path full_file_path = output_base_path_ / file_name;
            fs::create_directories(full_file_path.parent_path());
            std::ofstream file(full_file_path, std::ios::binary);
            if (!file)
            {
                std::println("Failed to open file : {}", full_file_path.string());
                return false;
            }

            const int digit = static_cast<int>(i % 10);
            std::string line(bytes_per_line, static_cast<char>('0' + digit));
            const uint64_t full_lines = bytes_per_file / bytes_per_line;
            const uint64_t remaining = bytes_per_file % bytes_per_line;
            const uint64_t expected_size = full_lines * (bytes_per_line + 1) + remaining;

            for (uint64_t k = 0; k < full_lines; ++k)
            {
                file << line << "\n";
            }
            if (remaining > 0)
            {
                file << std::string(remaining, static_cast<char>('0' + digit));
            }

            file.flush();
            if (!file)
            {
                std::println("flush preset file failed {}", full_file_path.string());
                return false;
            }
            file.close();
            if (!file)
            {
                std::println("close preset file failed {}", full_file_path.string());
                return false;
            }
            total_file_size += expected_size;
            ++file_cnt;
            ++i;
        }
        return true;
    }

    bool FileGenerator::GenerateRandomFiles(uint64_t first_level_dir_count, uint64_t max_dir_depth, uint64_t file_count,
                                            uint64_t min_file_size, uint64_t max_file_size, bool create_junk,
                                            CompressLevel compress)
    {
        file_cnt = 0;
        total_file_size = 0;

        if (create_junk)
        {
            if (auto st = CreateJunkFiles(output_base_path_); !st)
                return st;
        }

        std::vector<std::filesystem::path> dir_pool;
        BuildDirectoryPool(first_level_dir_count, max_dir_depth, dir_pool);

        if (dir_pool.empty())
        {
            std::println("directory pool is empty, cannot create any files");
            return false;
        }
        std::uniform_int_distribution<uint64_t> dir_choose{0, dir_pool.size() - 1};
        std::uniform_int_distribution<uint64_t> file_size_dist{min_file_size, max_file_size};

        const uint64_t digits = calc_digit_width(file_count);

        for (uint64_t i = 0; i < file_count; ++i)
        {
            const auto& target_dir = dir_pool[dir_choose(*gen_)];
            auto file_path = target_dir / std::format("rand_file_{:0{}}.bin", i, digits);
            const auto file_sz = file_size_dist(*gen_);

            if (const auto res = WriteRandomFile(file_path, file_sz, compress); !res)
            {
                return res;
            }
            total_file_size += file_sz;
            file_cnt += 1;
        }
        return true;
    }


    [[nodiscard]] bool FileGenerator::CheckPresetFilesReady() const
    {
        return std::ranges::all_of(preset_file_names_,
                                   [this](const auto& file_name) { return fs::exists(output_base_path_ / file_name); });
    }


} // namespace ctar
