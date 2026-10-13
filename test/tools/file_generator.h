#ifndef CTAR_FILE_GENERATOR_H
#define CTAR_FILE_GENERATOR_H
#include <charconv>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>


namespace ctar::test
{
    namespace fs = std::filesystem;

    static const std::unordered_set<std::string> junk_files = {".DS_Store", "._.DS_Store"};

    enum class CompressLevel
    {
        HighCompress,
        MediumCompress,
        NoCompress
    };

    enum class RandomSeedMode
    {
        FIXED,
        AUTO
    };

    constexpr std::string_view kCompressedFileSuffix = "rar";
    constexpr std::string_view kNormalFileSuffix = "bin";

    template <typename T>
    bool ParseUnsigned(std::string_view text, T& value)
        requires std::is_unsigned_v<T>
    {
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        return ec == std::errc{} && ptr == text.data() + text.size();
    }

    struct GeneratorOption
    {
        fs::path output_dir = "./test_data";
        CompressLevel compress_level = CompressLevel::NoCompress;
        RandomSeedMode random_seed_mode = RandomSeedMode::AUTO;
        bool random_mode = true;
        bool create_junk_file = false;
        bool create_zero_file = false;

        // File count interval for zero-sized files.
        // File count interval for tar file
        uint32_t zero_file_interval = 20;
        uint32_t compressed_file_interval = 25;

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


    class FileGenerator
    {
    private:
        fs::path output_base_path_;
        mutable std::optional<std::mt19937_64> gen_;
        inline static std::random_device rd_;

        std::uniform_int_distribution<std::uint8_t> byte_dist_;
        std::uniform_int_distribution<uint64_t> depth_dist_;
        std::uniform_int_distribution<uint64_t> file_size_dist_;
        std::uniform_int_distribution<uint64_t> dir_rand_{0, 8};
        std::uniform_int_distribution<uint64_t> first_dir_selector_;
        static constexpr uint64_t kChunkSize = 4096;
        static constexpr uint64_t kJunkFileSize = 512;

        static constexpr std::array<std::string_view, 10> preset_file_names_ = {"log/file0.txt",
                                                                                "log/file1.txt",
                                                                                "config/device/file2.txt",
                                                                                "config/device/file3.txt",
                                                                                "config/network/file4.txt",
                                                                                "resource/bin/file5.bin",
                                                                                "resource/bin/file6.bin",
                                                                                "media/sensor/1/file7.bin",
                                                                                "media/sensor/1/file8.bin",
                                                                                "media/sensor/1/file9.bin"};

        [[nodiscard]] bool WriteRandomFile(const fs::path& file_path, uint64_t file_size,
                                           CompressLevel compress_level) const;
        [[nodiscard]] bool CreateJunkFiles(const fs::path& dir) const;

        void BuildDirectoryPool(uint64_t first_level_dir_count, uint64_t max_dir_depth,
                                std::vector<std::filesystem::path>& dir_pool) const;

    public:
        uint64_t file_cnt = 0;
        uint64_t total_file_size = 0;

        explicit FileGenerator(fs::path base_path, RandomSeedMode random_seed_mode = RandomSeedMode::AUTO);

        static std::array<std::string_view, 10> GetPresetFiles() { return preset_file_names_; }

        [[nodiscard]] std::vector<std::string> GetPresetDirs() const;

        bool GenerateRandomFiles(const GeneratorOption& option);

        [[nodiscard]] bool GeneratePresetFiles(uint64_t bytes_per_file = 60, uint64_t bytes_per_line = 20);

        [[nodiscard]] bool CheckPresetFilesReady() const;
    };


} // namespace ctar::test

#endif // CTAR_FILE_GENERATOR_H
