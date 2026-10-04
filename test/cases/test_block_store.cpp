#include <csignal>
#include <gtest/gtest.h>
#include <ranges>
#include <string>

#include "src/core/block_store.h"
#include "src/core/file_handle.h"
#include "test/tools/dir_compare.h"
#include "test/tools/file_generator.h"
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

        constexpr size_t magic_len = sizeof(MagicNumber);

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
        if (head_magic != MagicNumber)
        {
            return testing::AssertionFailure() << "Head magic mismatch! expected=0x" << std::hex << MagicNumber
                                               << ", actual=0x" << std::hex << head_magic << ", file=" << pack_file;
        }

        // get file size for tail magic offset
        auto file_size_status = read_file->GetFileSize();
        if (!file_size_status)
        {
            return testing::AssertionFailure() << "GetFileSize failed, file: " << pack_file;
        }
        const off_t file_sz = file_size_status.value();
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
        if (uint32_t tail_magic = DecodeFixed32(buf.data()); tail_magic != MagicNumber)
        {
            return testing::AssertionFailure()
                << "Tail magic mismatch! expected=0x" << std::hex << MagicNumber << ", actual=0x" << std::hex
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

        size_t test_file_count_ = 5000;
        size_t first_level_dir_count_ = 10;
        size_t max_dir_depth_ = 5;
        size_t min_file_size_ = 128;
        size_t max_file_size_ = 2048000;
        size_t create_junk_ = false;

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
            if (auto st = generator_random.GenerateRandomFiles(first_level_dir_count_, max_dir_depth_, test_file_count_,
                                                               min_file_size_, max_file_size_, create_junk_);
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


} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
