#ifndef CTAR_ARG_PARSER_H
#define CTAR_ARG_PARSER_H
#include <string>
#include "api/ctar.h"
#include "src/core/options.h"
namespace ctar::cli
{
    enum class CommandType : uint8_t
    {
        UNKNOWN = 0,
        HELP,
        PACK,
        UNPACK,
        TREE,
        LS,
        DUMP,
        VERSION,
        HASH,
        STAT
    };

    constexpr std::string PackFileSuffix = ".ctar";

    [[nodiscard]] constexpr CommandType GetCommandType(std::string_view arg) noexcept
    {
        if (arg == "--help" || arg == "help")
            return CommandType::HELP;
        if (arg == "--version" || arg == "-v" || arg == "version")
            return CommandType::VERSION;
        if (arg == "pack")
            return CommandType::PACK;
        if (arg == "unpack")
            return CommandType::UNPACK;
        if (arg == "tree")
            return CommandType::TREE;
        if (arg == "ls")
            return CommandType::LS;
        if (arg == "dump")
            return CommandType::DUMP;
        if (arg == "hash")
            return CommandType::HASH;
        if (arg == "stat")
            return CommandType::STAT;
        return CommandType::UNKNOWN;
    }

    struct CliOption
    {
        CommandType cmd{CommandType::UNKNOWN};
        std::string source_path;
        std::string output_path;
        std::string pack_file_path;
        std::string specified_path;
        std::string dump_file_path;
        CompressionConfig comp_config;
        unsigned thread_cnt = 0;
        bool recursive{true};
        bool enable_human_readable = false;
        bool enable_tree_dir_only = false;
        bool enable_tree_stats = false;
        bool enable_dump_header = false;

        std::string input_data;
        bool enable_hash_file = false;
    };


    void PrintHelp();
    Status ParseArgs(int argc, char** argv, CliOption& opt);
    [[nodiscard]] Status ParseArgs(const std::vector<std::string_view>& args, CliOption& opt);
    ResultStatus<FileStats> RunCommand(const CliOption& opt);


} // namespace ctar::cli
#endif // CTAR_ARG_PARSER_H
