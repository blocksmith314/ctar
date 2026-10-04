#ifndef CTAR_FILE_GENERATOR_H
#define CTAR_FILE_GENERATOR_H
#include <charconv>
#include <filesystem>
#include <random>
#include <string>
#include <cstring>
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

    template <typename T>
    bool ParseUnsigned(std::string_view text, T& value)
        requires std::is_unsigned_v<T>
    {
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        return ec == std::errc{} && ptr == text.data() + text.size();
    }


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

        inline static const std::vector<std::string> preset_file_names_ = {"log/file0.txt",
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

        static std::vector<std::string> GetPresetFiles() { return preset_file_names_; }

        [[nodiscard]] std::vector<std::string> GetPresetDirs() const;


        /// Generate a set of random test files, with auto-created nested directory tree.
        /// @param first_level_dir_count Number of top-level directories
        /// @param max_dir_depth Maximum depth of directory tree
        /// @param file_count Total random data files to generate
        /// @param min_file_size Minimum byte size of each generated file
        /// @param max_file_size Maximum byte size of each generated file
        /// @param create_junk Whether create junk files under root directory
        /// @param compress Content entropy level for generated files
        /// @return Status OK on success, error immediately if any step fails
        bool GenerateRandomFiles(uint64_t first_level_dir_count, uint64_t max_dir_depth, uint64_t file_count,
                                   uint64_t min_file_size, uint64_t max_file_size, bool create_junk,
                                   CompressLevel compress = CompressLevel::NoCompress);
        [[nodiscard]] bool GeneratePresetFiles(uint64_t bytes_per_file = 60, uint64_t bytes_per_line = 20);
        [[nodiscard]] bool CheckPresetFilesReady() const;
    };


} // namespace ctar

#endif // CTAR_FILE_GENERATOR_H
