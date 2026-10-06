#include <gtest/gtest.h>

#include "src/cli/arg_parser.h"
#include "test/tools/dir_compare.h"
#include "test/tools/fixture/blob_store_fixture.h"
#include "test/tools/test_helpers.h"
#include "xxhash.h"

namespace ctar
{
    TEST(ArgParserTest, NoSubCommandReturnsHelp)
    {
        cli::CliOption opt{};
        const std::vector<std::string_view> args{"ctar"};
        auto status = cli::ParseArgs(args, opt);
        EXPECT_FALSE(status);
    }

    TEST(ArgParserTest, VERSION)
    {
        cli::CliOption opt{};
        const std::vector<std::string_view> args{"ctar", "version"};
        auto status = cli::ParseArgs(args, opt);
        EXPECT_TRUE(status);
    }

    TEST(ArgParserTest, HASH)
    {
        cli::CliOption opt{};
        Status status;
        {
            const std::vector<std::string_view> args{"ctar", "hash", "hello"};
            status = cli::ParseArgs(args, opt);
            ASSERT_EQ(opt.cmd, cli::CommandType::HASH);
            ASSERT_FALSE(opt.enable_hash_file);
            ASSERT_EQ(opt.input_data, "hello");
            EXPECT_TRUE(status);
        }
        {
            const std::vector<std::string_view> args{"ctar", "hash", "-f", "xx.txt"};
            status = cli::ParseArgs(args, opt);
            ASSERT_EQ(opt.cmd, cli::CommandType::HASH);
            ASSERT_TRUE(opt.enable_hash_file);
            ASSERT_EQ(opt.input_data, "xx.txt");
            EXPECT_TRUE(status);
        }
        {
            const std::vector<std::string_view> args{"ctar", "hash", "-f"};
            status = cli::ParseArgs(args, opt);
            EXPECT_FALSE(status);
        }
    }

    TEST(ArgParserTest, STAT)
    {
        cli::CliOption opt{};
        Status status;
        {
            const std::vector<std::string_view> args{"ctar", "stat", "xx.ctar"};
            status = cli::ParseArgs(args, opt);
            ASSERT_EQ(opt.cmd, cli::CommandType::STAT);
            ASSERT_EQ(opt.pack_file_path, "xx.ctar");
            EXPECT_TRUE(status);
        }
        {
            const std::vector<std::string_view> args{"ctar", "stat"};
            status = cli::ParseArgs(args, opt);
            EXPECT_FALSE(status);
        }
        {
            const std::vector<std::string_view> args{"ctar", "stat", "xx"};
            status = cli::ParseArgs(args, opt);
            EXPECT_FALSE(status);
        }
    }

    TEST(ArgParserTest, CommandPackValid)
    {
        cli::CliOption opt{};
        const std::vector<std::string_view> args{"ctar", "pack", "./src", "out.ctar"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::PACK);
        EXPECT_EQ(opt.source_path, "./src");
        EXPECT_EQ(opt.output_path, "out.ctar");
    }

    TEST(ArgParserTest, CommandPackCompressNone)
    {
        cli::CliOption opt{};
        const std::vector<std::string_view> args{"ctar", "pack", "-c", "none", "./src", "out.ctar"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::PACK);
        EXPECT_EQ(opt.source_path, "./src");
        EXPECT_EQ(opt.output_path, "out.ctar");
        EXPECT_EQ(opt.comp_config.compression_type, CompressionType::kNone);
    }

    TEST(ArgParserTest, CommandPackCompressLZ4)
    {
        {
            cli::CliOption opt{};
            const std::vector<std::string_view> args{"ctar", "pack", "-c", "lz4", "-p", "10", "./src", "out.ctar"};
            auto ok = cli::ParseArgs(args, opt);

            EXPECT_TRUE(ok);
            EXPECT_EQ(opt.cmd, cli::CommandType::PACK);
            EXPECT_EQ(opt.source_path, "./src");
            EXPECT_EQ(opt.output_path, "out.ctar");
            EXPECT_EQ(opt.comp_config.compression_type, CompressionType::kLZ4);
            EXPECT_EQ(opt.comp_config.compression_param, 10);
        }
        {
            cli::CliOption opt{};
            const std::vector<std::string_view> args{"ctar", "pack", "-c", "lz4", "./src", "out.ctar"};
            auto ok = cli::ParseArgs(args, opt);

            EXPECT_TRUE(ok);
            EXPECT_EQ(opt.cmd, cli::CommandType::PACK);
            EXPECT_EQ(opt.source_path, "./src");
            EXPECT_EQ(opt.output_path, "out.ctar");
            EXPECT_EQ(opt.comp_config.compression_type, CompressionType::kLZ4);
            EXPECT_EQ(opt.comp_config.compression_param, kLZ4AccDefault);
        }
    }

    TEST(ArgParserTest, CommandPackCompressLZ4HC)
    {
        {
            cli::CliOption opt{};
            const std::vector<std::string_view> args{"ctar", "pack", "-c", "lz4hc", "-p", "10", "./src", "out.ctar"};
            auto ok = cli::ParseArgs(args, opt);

            EXPECT_TRUE(ok);
            EXPECT_EQ(opt.cmd, cli::CommandType::PACK);
            EXPECT_EQ(opt.source_path, "./src");
            EXPECT_EQ(opt.output_path, "out.ctar");
            EXPECT_EQ(opt.comp_config.compression_type, CompressionType::kLZ4HC);
            EXPECT_EQ(opt.comp_config.compression_param, 10);
        }

        {
            cli::CliOption opt{};
            const std::vector<std::string_view> args{"ctar", "pack", "-c", "lz4hc", "./src", "out.ctar"};
            auto ok = cli::ParseArgs(args, opt);

            EXPECT_TRUE(ok);
            EXPECT_EQ(opt.cmd, cli::CommandType::PACK);
            EXPECT_EQ(opt.source_path, "./src");
            EXPECT_EQ(opt.output_path, "out.ctar");
            EXPECT_EQ(opt.comp_config.compression_type, CompressionType::kLZ4HC);
            EXPECT_EQ(opt.comp_config.compression_param, kLZ4HCDefault);
        }
    }

    TEST(ArgParserTest, CommandPackMissingArgument)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "pack", "./src"};
        auto ok = cli::ParseArgs(args, opt);
        EXPECT_FALSE(ok);
    }

    TEST(ArgParserTest, CommandUnpackValid)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "unpack", "xxx.ctar", "./outdir"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::UNPACK);
        EXPECT_EQ(opt.source_path, "xxx.ctar");
        EXPECT_EQ(opt.output_path, "./outdir");
    }

    TEST(ArgParserTest, TreeLocalDirectory)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "tree", "docs"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::TREE);
        EXPECT_EQ(opt.specified_path, "docs");
        EXPECT_TRUE(opt.pack_file_path.empty());
    }

    TEST(ArgParserTest, TreePathTrailingSlashTrimmed)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "tree", "docs/"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.specified_path, "docs");
    }

    TEST(ArgParserTest, TreeFromArchiveFile)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{
            "ctar",
            "tree",
            "test.ctar",
            "inner_dir",
        };
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::TREE);
        EXPECT_EQ(opt.specified_path, "inner_dir");
        EXPECT_EQ(opt.pack_file_path, "test.ctar");
    }

    TEST(ArgParserTest, TreeUnknownOption)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "tree", "-z"};
        auto ok = cli::ParseArgs(args, opt);
        EXPECT_FALSE(ok);
    }

    TEST(ArgParserTest, LsArchiveOnly)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "ls", "demo.ctar"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::LS);
        EXPECT_EQ(opt.pack_file_path, "demo.ctar");
        EXPECT_FALSE(opt.enable_human_readable);
    }

    TEST(ArgParserTest, LsHumanReadableFlag)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "ls", "-h", "demo.ctar"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_TRUE(opt.enable_human_readable);
        EXPECT_EQ(opt.pack_file_path, "demo.ctar");
    }

    TEST(ArgParserTest, LsInnerPathWithTrailingSlash)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "ls", "demo.ctar", "data/"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.specified_path, "data");
        EXPECT_EQ(opt.pack_file_path, "demo.ctar");
    }

    TEST(ArgParserTest, DumpStdout)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "dump", "demo.ctar"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.cmd, cli::CommandType::DUMP);
        EXPECT_TRUE(opt.dump_file_path.empty());
    }

    TEST(ArgParserTest, DumpOutputFileOption)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "dump", "-o", "out.tsv", "demo.ctar"};
        auto ok = cli::ParseArgs(args, opt);

        EXPECT_TRUE(ok);
        EXPECT_EQ(opt.dump_file_path, "out.tsv");
        EXPECT_EQ(opt.pack_file_path, "demo.ctar");
    }

    TEST(ArgParserTest, DumpMissingPathAfterO)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "dump", "-o"};
        auto ok = cli::ParseArgs(args, opt);
        EXPECT_FALSE(ok);
    }

    TEST(ArgParserTest, UnknownCommand)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "foobar"};
        auto ok = cli::ParseArgs(args, opt);
        EXPECT_FALSE(ok);
    }


    class CommandPresetFileTest : public test::PresetFileFixture
    {
    protected:
        void SetUp() override
        {
            bytes_per_file_ = 256;
            bytes_per_line_ = 64;
            PresetFileFixture::SetUp();
        }

    public:
        std::string arg_source = source_dir_;
        std::string arg_pack_file = pack_file;
        std::string arg_unpack_dir = unpack_dir_;
    };

    TEST(CommandTest, Version)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "version"};
        auto status = ParseArgs(args, opt);
        EXPECT_TRUE(status);
        std::ostringstream output_buf;
        {
            test::CoutRedirectGuard redirect(output_buf);
            auto result_status = cli::RunCommand(opt);
            EXPECT_TRUE(result_status);
        }
        std::string version = output_buf.str();
        ASSERT_TRUE(version.contains("ctar"));
        ASSERT_TRUE(version.contains(kToolVersion.to_string()));
        ASSERT_TRUE(version.contains("file format"));
        ASSERT_TRUE(version.contains(kFileFormatVersion.to_string()));
    }

    TEST(CommandTest, HASH)
    {
        cli::CliOption opt{};
        std::string input_data = "hello";
        std::vector<std::string_view> args{"ctar", "hash", input_data};
        auto status = ParseArgs(args, opt);
        EXPECT_TRUE(status);
        std::ostringstream output_buf;
        {
            test::CoutRedirectGuard redirect(output_buf);
            auto result_status = cli::RunCommand(opt);
            EXPECT_TRUE(result_status);
        }
        auto result = XXH64(input_data.data(), input_data.size(), kHashSeed);
        std::string hash_value = output_buf.str();
        std::string target_result = std::format("0x{:016x}\n", result);
        ASSERT_EQ(hash_value, target_result);
    }

    TEST_F(CommandPresetFileTest, STAT)
    {
        {
            cli::CliOption opt{};
            std::vector<std::string_view> args{"ctar", "pack", arg_source, arg_pack_file};
            auto status = ParseArgs(args, opt);

            EXPECT_TRUE(status);
            auto result_status = cli::RunCommand(opt);
            EXPECT_TRUE(result_status);
        }
        {
            cli::CliOption opt{};
            std::vector<std::string_view> args{"ctar", "stat", arg_pack_file};
            auto status = ParseArgs(args, opt);
            EXPECT_TRUE(status);
            {
                auto result_status = cli::RunCommand(opt);
                EXPECT_TRUE(result_status);
                auto file_stats = result_status.value();
                ASSERT_EQ(file_stats.total_file_count, generator_file_cnt);
                ASSERT_EQ(file_stats.total_original_size, generator_file_size);
                ASSERT_LT(file_stats.total_compressed_size, generator_file_size);
                ASSERT_GT(file_stats.padding_size, 0);
            }
        }
    }

    TEST_F(CommandPresetFileTest, Pack)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "pack", arg_source, arg_pack_file};
        auto status = ParseArgs(args, opt);

        EXPECT_TRUE(status);
        auto result_status = cli::RunCommand(opt);
        EXPECT_TRUE(result_status);
        fs::path file_path{arg_pack_file};
        const size_t size_bytes = fs::file_size(file_path);
        EXPECT_GT(size_bytes, 0);
    }

    TEST_F(CommandPresetFileTest, PackWithoutCompress)
    {
        cli::CliOption opt{};
        std::vector<std::string_view> args{"ctar", "pack", "-c", "none", arg_source, arg_pack_file};
        auto status = ParseArgs(args, opt);

        EXPECT_TRUE(status);
        auto result_status = cli::RunCommand(opt);
        EXPECT_TRUE(result_status);
        auto file_stats = result_status.value();
        ASSERT_EQ(file_stats.total_file_count, generator_file_cnt);
        ASSERT_EQ(file_stats.total_original_size, generator_file_size);
        ASSERT_EQ(file_stats.total_compressed_size, generator_file_size);
        ASSERT_GT(file_stats.padding_size, 0);
    }

    TEST_F(CommandPresetFileTest, UnPack)
    {
        cli::CliOption pack_opt{};
        std::vector<std::string_view> pack_args{"ctar", "pack", arg_source, arg_pack_file};
        auto status = ParseArgs(pack_args, pack_opt);
        EXPECT_TRUE(status) << "failed to parse arg of ctar";
        auto result_status = cli::RunCommand(pack_opt);
        EXPECT_TRUE(result_status) << "failed to execute ctar command";

        fs::path archive_path{arg_pack_file};
        ASSERT_TRUE(fs::exists(archive_path)) << "archive file was not created";

        const size_t size_bytes = fs::file_size(archive_path);
        EXPECT_GT(size_bytes, 0) << "archive file size should > 0";

        cli::CliOption unpack_opt{};
        std::vector<std::string_view> unpack_args{"CTAR_cli", "unpack", arg_pack_file, arg_unpack_dir};
        status = ParseArgs(unpack_args, unpack_opt);
        EXPECT_TRUE(status) << "failed to parse arg of unpack";
        result_status = cli::RunCommand(unpack_opt);
        ASSERT_TRUE(result_status) << "failed to execute unpack command";

        const bool is_equal = test::AreDirectoriesEqual(source_dir_, unpack_dir_ / source_dir_);
        EXPECT_TRUE(is_equal) << "the dir is not match";
    }

    TEST_F(CommandPresetFileTest, TreeArchiveContent)
    {
        cli::CliOption pack_opt{};
        std::vector<std::string_view> pack_args{"ctar", "pack", arg_source, arg_pack_file};
        auto status = ParseArgs(pack_args, pack_opt);
        ASSERT_TRUE(status) << "ctar parse failed";
        auto result_status = cli::RunCommand(pack_opt);
        ASSERT_TRUE(result_status) << "ctar run failed";

        cli::CliOption tree_opt{};
        std::vector<std::string_view> tree_args{"ctar", "tree", arg_pack_file};
        status = ParseArgs(tree_args, tree_opt);
        ASSERT_TRUE(status) << "tree parse failed";

        std::ostringstream output_buf;
        {
            test::CoutRedirectGuard redirect(output_buf);
            result_status = cli::RunCommand(tree_opt);
            ASSERT_TRUE(result_status) << "tree command run failed";
            ;
        }

        std::string tree_text = output_buf.str();

        for (const auto& keyword : preset_key_words)
        {
            EXPECT_NE(tree_text.find(keyword), std::string::npos) << "tree output missing keyword: " << keyword;
        }
    }

    TEST_F(CommandPresetFileTest, DumpMetadataToStdout)
    {
        cli::CliOption pack_opt{};
        std::vector<std::string_view> pack_args{"ctar", "pack", arg_source, arg_pack_file};
        auto status = ParseArgs(pack_args, pack_opt);
        ASSERT_TRUE(status);
        auto result_status = cli::RunCommand(pack_opt);
        ASSERT_TRUE(result_status);

        cli::CliOption dump_opt{};
        std::vector<std::string_view> dump_args{"ctar", "dump", arg_pack_file};
        status = ParseArgs(dump_args, dump_opt);
        ASSERT_TRUE(status);

        std::ostringstream output_buf;
        {
            test::CoutRedirectGuard redirect(output_buf);
            result_status = cli::RunCommand(dump_opt);
            ASSERT_TRUE(result_status);
        }

        std::string dump_text = output_buf.str();
        for (const auto& keyword : preset_key_words)
        {
            EXPECT_NE(dump_text.find(keyword), std::string::npos) << "dump output missing keyword: " << keyword;
        }
    }

    TEST_F(CommandPresetFileTest, DumpMetadataWithTitleToStdout)
    {
        cli::CliOption pack_opt{};
        std::vector<std::string_view> pack_args{"ctar", "pack", arg_source, arg_pack_file};
        auto status = ParseArgs(pack_args, pack_opt);
        ASSERT_TRUE(status);
        auto result_status = cli::RunCommand(pack_opt);
        ASSERT_TRUE(result_status);

        cli::CliOption dump_opt{};
        std::vector<std::string_view> dump_args{"ctar", "dump", "-H", arg_pack_file};
        status = ParseArgs(dump_args, dump_opt);
        ASSERT_TRUE(status);

        std::ostringstream output_buf;
        {
            test::CoutRedirectGuard redirect(output_buf);
            result_status = cli::RunCommand(dump_opt);
            ASSERT_TRUE(result_status);
        }
        ASSERT_TRUE(status);

        std::string dump_text = output_buf.str();
        for (const auto& keyword : preset_key_words)
        {
            EXPECT_NE(dump_text.find(keyword), std::string::npos) << "dump output missing keyword: " << keyword;
        }
        EXPECT_NE(dump_text.find(kTsvMetaHeader), std::string::npos) << "Dump output missing TSV header";
    }


    TEST_F(CommandPresetFileTest, DumpMetadataToTsv)
    {
        cli::CliOption pack_opt{};
        std::vector<std::string_view> pack_args{"ctar", "pack", arg_source, arg_pack_file};
        auto status = ParseArgs(pack_args, pack_opt);
        ASSERT_TRUE(status);
        auto result_status = cli::RunCommand(pack_opt);
        ASSERT_TRUE(result_status);

        std::string tsv_output_path = unpack_dir_ / "meta_export.tsv";

        // dump -o xxx.tsv archive.ctar
        cli::CliOption dump_opt{};
        std::vector<std::string_view> dump_args{"ctar", "dump", "-o", tsv_output_path, arg_pack_file};
        status = ParseArgs(dump_args, dump_opt);
        ASSERT_TRUE(status);
        result_status = cli::RunCommand(dump_opt);
        ASSERT_TRUE(result_status);

        ASSERT_TRUE(fs::exists(tsv_output_path));
        const auto tsv_size = fs::file_size(tsv_output_path);
        EXPECT_GT(tsv_size, 0);
    }


} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
// test_cli.cpp
