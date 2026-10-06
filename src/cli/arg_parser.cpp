#include <fstream>
#include <iostream>
#include <xxhash.h>

#include "api/ctar.h"
#include "src/cli/arg_parser.h"


namespace
{
    constexpr const char* kUsageText = R"USAGE(ctar - pack and unpack directory trees into a single archive

USAGE
    ctar [global-options] <command> [command-options] [arguments]

COMMANDS
    pack <input-dir> <output-archive>
        Pack a directory tree into an archive.

    unpack <archive> <output-dir>
        Extract an archive into a directory.

    tree <path>
        Print the directory tree of a local folder or an archive.
        <path> may be either one; it is auto-detected.

    ls [options] <archive> [inner-dir]
        List file metadata stored in an archive.
        With <inner-dir>, list only entries under that directory.

    dump [options] <archive>
        Export archive metadata as TSV (tab-separated values).

    hash [options] <input-data>
        Calculate XXHASH (XXH64) of input data or file (if '-f' provide)

GLOBAL OPTIONS
    --help
        Print this message and exit.

    -V, --version
        Print version information and exit.

    -t, --threads NUM
        Number of worker threads for pack and unpack.
        Default: number of CPU cores.

COMMAND OPTIONS
    pack:
        -c, --compression {lz4,lz4hc}
            Compression algorithm (default: lz4).
                lz4     fast, lower ratio
                lz4hc   slow, higher ratio
                none    raw data

        -p NUM
            Tuning parameter.  Its meaning depends on --compression:

                -c lz4      acceleration factor, 1..65537        (default 1)
                            Higher is faster and compresses less; each step
                            buys roughly +3% speed.

                -c lz4hc    compression level, 2..12              (default 9)
                            Levels 10..12 use the optimal parser and are
                            much slower.

            If --compression is omitted, NUM is read as lz4 acceleration.
    ls:
        -h
            Print sizes in human-readable form (1024-based: K, M, G).

    dump:
        -o FILE
            Write TSV to FILE instead of stdout.
        -H, --header
            Prepend a header row with the column names.

    hash:
        -f FILE
            Get the XXHASH (XXH64) of the file


EXIT STATUS
    0    success
    1    error (I/O failure, corrupt archive, checksum mismatch, unknown command, missing or invalid argument)

EXAMPLES
    ctar pack ./data archive.ctar
    ctar pack -t 8 ./data archive.ctar
    ctar pack -t 8 -c lz4 -p 1 ./data archive.ctar
    ctar unpack archive.ctar ./restore
    ctar tree archive.ctar
    ctar ls -h archive.ctar
    ctar ls archive.ctar sub/dir
    ctar dump archive.ctar
    ctar dump -H -o meta.tsv archive.ctar
    ctar dump -H -o meta.tsv archive.ctar sub/dir
    ctar hash hello
    ctar -f xx.txt

TSV COLUMNS (dump), in order
    block_id  dir_id  file_id  permissions  owner  group  modify_time
    offset  original_size  compressed_size  dir_name  file_name

    Fields are separated by TAB and records by LF

Run 'ctar <command> --help' for the full option list of a command.
)USAGE";
} // namespace

namespace ctar::cli
{
    void PrintHelp() { std::cout << kUsageText; }

    inline std::string TrimTrailingSlash(std::string_view path)
    {
        if (path.empty())
        {
            return {};
        }
        size_t end = path.size();
        while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\'))
        {
            --end;
        }
        return std::string(path.substr(0, end));
    }

    Status parse_common_options(int argc, char** argv, int& pos, CliOption& opt, std::string_view subcommand_name)
    {
        constexpr size_t MAX_THREADS = 16U;
        while (pos < argc && argv[pos][0] == '-')
        {
            std::string_view op{argv[pos]};
            if (op == "-t" || op == "--threads")
            {
                ++pos;
                if (pos >= argc)
                {
                    return error_invalid_arg("missing thread count value after {}", op);
                }
                char* end_ptr = nullptr;
                long val = std::strtol(argv[pos], &end_ptr, 10);
                if (*end_ptr != '\0' || val <= 0)
                {
                    return error_invalid_arg("invalid thread number '{}', must be positive integer", argv[pos]);
                }
                size_t thread_num = static_cast<size_t>(val);
                if (thread_num > MAX_THREADS)
                {
                    thread_num = MAX_THREADS;
                }
                opt.thread_cnt = thread_num;
                ++pos;
            }
            else if (op == "-c" || op == "--compression")
            {
                ++pos;
                if (pos >= argc)
                {
                    return error_invalid_arg("missing compression type after {}", op);
                }
                std::string_view ctype = argv[pos];
                if (ctype == "lz4")
                {
                    opt.comp_config.compression_type = CompressionType::kLZ4;
                    opt.comp_config.compression_param = kLZ4AccDefault;
                }
                else if (ctype == "lz4hc")
                {
                    opt.comp_config.compression_type = CompressionType::kLZ4HC;
                    opt.comp_config.compression_param = kLZ4HCDefault;
                }
                else if (ctype == "none")
                {
                    opt.comp_config.compression_type = CompressionType::kNone;
                    opt.comp_config.compression_param = 0;
                }
                else
                {
                    return error_invalid_arg("unsupported compression type '{}', available: lz4, lz4hc, none", ctype);
                }
                ++pos;
            }
            else if (op == "-p" || op == "--param")
            {
                ++pos;
                if (pos >= argc)
                {
                    return error_invalid_arg("missing compression parameter value after {}", op);
                }
                char* end_ptr = nullptr;
                const long val = std::strtol(argv[pos], &end_ptr, 10);
                if (*end_ptr != '\0')
                {
                    return error_invalid_arg("invalid compression param '{}', must be integer", argv[pos]);
                }
                opt.comp_config.compression_param = static_cast<int>(val);
                ++pos;
            }
            else
            {
                return error_invalid_arg("unknown option for {}: {}", subcommand_name, op);
            }
        }
        if (opt.comp_config.compression_type == CompressionType::kLZ4)
        {
            if (opt.comp_config.compression_param < kLZ4AccDefault || opt.comp_config.compression_param > kLZ4AccMax)
            {
                return error_invalid_arg("lz4 acceleration must between {} and {}, got {}", kLZ4AccDefault, kLZ4AccMax,
                                         opt.comp_config.compression_param);
            }
        }
        else if (opt.comp_config.compression_type == CompressionType::kLZ4HC)
        {
            if (opt.comp_config.compression_param < kLZ4HCMin || opt.comp_config.compression_param > kLZ4HCMax)
            {
                return error_invalid_arg("lz4hc compression level must be between {} and {}, got {}", kLZ4HCMin,
                                         kLZ4HCMax, opt.comp_config.compression_param);
            }
        }
        return {};
    }

    Status ParseArgs(int argc, char** argv, CliOption& opt)
    {
        if (argc < 2)
        {
            return error_invalid_arg("no command specified");
        }
        int pos = 1;
        const std::string_view first_arg{argv[pos]};
        const auto command = GetCommandType(first_arg);
        if (command == CommandType::UNKNOWN)
        {
            return error_invalid_arg("unknown command: {}", first_arg);
        }
        switch (command)
        {
        case CommandType::HELP:
            {
                opt.cmd = CommandType::HELP;
                return {};
            }
        case CommandType::VERSION:
            {
                opt.cmd = CommandType::VERSION;
                return {};
            }
        case CommandType::HASH:
            {
                opt.cmd = CommandType::HASH;
                pos++;
                while (pos < argc && argv[pos][0] == '-')
                {
                    std::string_view op{argv[pos]};
                    if (op == "-f")
                        opt.enable_hash_file = true;
                    else
                    {
                        return error_invalid_arg("unknown option for hash: {}", op);
                    }
                    pos++;
                }
                int remain_argc = argc - pos;
                if (remain_argc != 1)
                {
                    return error_invalid_arg("missing arguments, expected input file or input string");
                }
                opt.input_data = argv[pos];
                return {};
            }
        case CommandType::STAT:
            {
                opt.cmd = CommandType::STAT;
                pos++;
                int remain_argc = argc - pos;
                if (remain_argc != 1)
                {
                    return error_invalid_arg("missing arguments, expected archive file");
                }
                opt.pack_file_path = argv[pos];
                if (!opt.pack_file_path.ends_with(PackFileSuffix))
                {
                    return error_invalid_arg("archive file must end with {}", PackFileSuffix);
                }
                return {};
            }
        case CommandType::PACK:
            {
                opt.cmd = CommandType::PACK;
                pos++;
                auto res = parse_common_options(argc, argv, pos, opt, "pack");
                if (!res)
                {
                    return res;
                }
                int remain_argc = argc - pos;
                if (remain_argc != 2)
                {
                    return error_invalid_arg("missing arguments, expected input directory and output archive path");
                }
                opt.source_path = TrimTrailingSlash(argv[pos++]);
                opt.output_path = TrimTrailingSlash(argv[pos]);
                return {};
            }
        case CommandType::UNPACK:
            {
                opt.cmd = CommandType::UNPACK;
                pos++;
                auto res = parse_common_options(argc, argv, pos, opt, "unpack");
                if (!res)
                {
                    return res;
                }
                int remain_argc = argc - pos;
                if (remain_argc != 2)
                {
                    return error_invalid_arg("missing arguments, expected archive path and output directory");
                }
                opt.source_path = TrimTrailingSlash(argv[pos++]);
                opt.output_path = TrimTrailingSlash(argv[pos]);
                return {};
            }
        case CommandType::TREE:
            {
                opt.cmd = CommandType::TREE;
                pos++;
                while (pos < argc && argv[pos][0] == '-')
                {
                    std::string_view op{argv[pos]};
                    if (op == "-d")
                        opt.enable_tree_dir_only = true;
                    else if (op == "-s")
                        opt.enable_tree_stats = true;
                    else
                    {
                        return error_invalid_arg("unknown option for tree: {}", op);
                    }
                    pos++;
                }
                int remain_argc = argc - pos;
                if (remain_argc == 0)
                {
                    opt.specified_path = ".";
                    return {};
                }
                else if (remain_argc == 1)
                {
                    auto temp = TrimTrailingSlash(argv[pos]);
                    temp.ends_with(PackFileSuffix) ? opt.pack_file_path = temp : opt.specified_path = temp;
                }
                else if (remain_argc == 2)
                {
                    opt.pack_file_path = argv[pos++];
                    opt.specified_path = TrimTrailingSlash(argv[pos]);
                    if (!opt.pack_file_path.ends_with(PackFileSuffix))
                    {
                        return error_invalid_arg("archive file must end with {}", PackFileSuffix);
                    }
                }
                else
                {
                    return error_invalid_arg("too many positional arguments for tree");
                }
                return {};
            }
        case CommandType::LS:
            {
                opt.cmd = CommandType::LS;
                pos++;
                while (pos < argc && argv[pos][0] == '-')
                {
                    std::string_view op{argv[pos]};
                    if (op == "-h")
                        opt.enable_human_readable = true;
                    else
                    {
                        return error_invalid_arg("unknown option for ls: {}", op);
                    }
                    pos++;
                }
                int remain_argc = argc - pos;
                if (remain_argc == 0)
                {
                    return error_invalid_arg("missing archive file path");
                }
                else if (remain_argc == 1)
                {
                    opt.pack_file_path = argv[pos];
                    if (!opt.pack_file_path.ends_with(PackFileSuffix))
                    {
                        return error_invalid_arg("archive file must end with {}", PackFileSuffix);
                    }
                }
                else if (remain_argc == 2)
                {
                    opt.pack_file_path = argv[pos++];
                    opt.specified_path = TrimTrailingSlash(argv[pos]);
                    if (!opt.pack_file_path.ends_with(PackFileSuffix))
                    {
                        return error_invalid_arg("archive file must end with {}", PackFileSuffix);
                    }
                }
                else
                {
                    return error_invalid_arg("too many positional arguments for ls");
                }
                return {};
            }
        case CommandType::DUMP:
            {
                opt.cmd = CommandType::DUMP;
                pos++;
                while (pos < argc && argv[pos][0] == '-')
                {
                    std::string_view op{argv[pos]};
                    if (op == "-o")
                    {
                        pos++;
                        if (pos >= argc || argv[pos][0] == '-')
                        {
                            return error_invalid_arg("missing output file path after -o");
                        }
                        opt.dump_file_path = argv[pos];
                    }
                    else if (op == "-H")
                    {
                        opt.enable_dump_header = true;
                    }
                    else
                    {
                        return error_invalid_arg("unknown option for dump: {}", op);
                    }
                    pos++;
                }
                int remain_argc = argc - pos;
                if (remain_argc == 1)
                {
                    opt.pack_file_path = argv[pos];
                    if (!opt.pack_file_path.ends_with(PackFileSuffix))
                    {
                        return error_invalid_arg("archive file must end with {}", PackFileSuffix);
                    }
                }
                else if (remain_argc == 2)
                {
                    opt.pack_file_path = argv[pos++];
                    opt.specified_path = TrimTrailingSlash(argv[pos]);
                    if (!opt.pack_file_path.ends_with(PackFileSuffix))
                    {
                        return error_invalid_arg("archive file must end with {}", PackFileSuffix);
                    }
                }
                else
                {
                    return error_invalid_arg("too many positional arguments for dump");
                }
                return {};
            }
        default:
            {
                return error_invalid_arg("unknown command");
            }
        }
    }

    Status ParseArgs(const std::vector<std::string_view>& args, CliOption& opt)
    {
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (auto& s : args)
        {
            argv.push_back(const_cast<char*>(s.data()));
        }
        return ParseArgs(static_cast<int>(argv.size()), argv.data(), opt);
    }

    ResultStatus<FileStats> RunCommand(const CliOption& opt)
    {
        unsigned hw = std::thread::hardware_concurrency();
        unsigned hw_thread_cnt = (hw == 0) ? 1 : (hw);
        unsigned worker_thread_cnt = opt.thread_cnt > 0 ? opt.thread_cnt : hw_thread_cnt;
        BlockStore store(worker_thread_cnt, opt.comp_config);
        switch (opt.cmd)
        {
        case CommandType::HELP:
            {
                PrintHelp();
            }
            break;
        case CommandType::VERSION:
            {
                std::println(std::cout, "ctar : {} (file format)", kToolVersion.to_string(),
                             kFileFormatVersion.to_string());
            }
            break;
        case CommandType::HASH:
            {
                uint64_t hash_value;
                if (opt.enable_hash_file)
                {
                    if (auto hash_status = Hash(opt.input_data))
                    {
                        hash_value = hash_status.value();
                    }
                    else
                    {
                        return std::unexpected(hash_status.error());
                    }
                }
                else
                {
                    hash_value = XXH64(opt.input_data.data(), opt.input_data.size(), kHashSeed);
                }
                std::println(std::cout, "0x{:016x}", hash_value);
            }
            break;
        case CommandType::STAT:
            {
                Status status = store.RestoreBlocksFromPack(opt.pack_file_path);
                if (status)
                {
                    return store.GetFileStats();
                }
                else
                {
                    return error_run_time("failed to restore meta from '{}': {}", opt.pack_file_path,
                                          status.error().message());
                }
            }
            break;
        case CommandType::PACK:
            {
                Status status = store.Scan(opt.source_path, opt.recursive);
                if (status)
                {
                    status = store.PackPipeline(opt.output_path);
                    if (!status)
                    {
                        return error_run_time("failed to pack directory '{}': {}", opt.source_path,
                                              status.error().message());
                    }
                }
                else
                {
                    return error_run_time("failed to scan directory '{}': {}", opt.source_path,
                                          status.error().message());
                }
            }
            break;
        case CommandType::UNPACK:
            {
                Status status = store.UnPackPipeline(opt.source_path, opt.output_path);
                if (!status)
                {
                    return error_run_time("failed to unpack archive '{}': {}", opt.source_path,
                                          status.error().message());
                }
            }
            break;
        case CommandType::TREE:
            {
                if (opt.enable_tree_dir_only || opt.enable_tree_stats)
                {
                    // TODO : show file stats of directory tree
                    return error_run_time("selected options are not yet implemented");
                }
                Status status;
                if (opt.pack_file_path.empty())
                {
                    status = store.Scan(opt.specified_path, opt.recursive);
                    if (!status)
                    {
                        return error_run_time("failed to scan directory '{}': {}", opt.specified_path,
                                              status.error().message());
                    }
                }
                else
                {
                    status = store.RestoreBlocksFromPack(opt.pack_file_path);
                    if (!status)
                    {
                        return error_run_time("failed to load archive '{}': {}", opt.pack_file_path,
                                              status.error().message());
                    }
                }
                status = store.RenderDirectoryTree(opt.specified_path, true);
                if (!status)
                {
                    return error_run_time("failed to print tree of target path '{}': {}", opt.specified_path,
                                          status.error().message());
                }
            }
            break;
        case CommandType::LS:
            {
                Status status = store.RestoreBlocksFromPack(opt.pack_file_path);
                if (!status)
                {
                    return error_run_time("failed to load archive '{}': {}", opt.pack_file_path,
                                          status.error().message());
                }
                status = store.DumpDirectoryTree(std::cout, opt.specified_path, opt.enable_human_readable);
                if (!status)
                {
                    return error_run_time("failed to list directory tree '{}': {}", opt.specified_path,
                                          status.error().message());
                }
            }
            break;
        case CommandType::DUMP:
            {
                Status status = store.RestoreBlocksFromPack(opt.pack_file_path);
                if (!status)
                {
                    return error_run_time("failed to load archive '{}': {}", opt.pack_file_path,
                                          status.error().message());
                }
                if (opt.dump_file_path.empty())
                {
                    status = store.DumpDirectoryTree(std::cout, opt.specified_path, false, OutputMode::TSV,
                                                     opt.enable_dump_header);
                    if (!status)
                    {
                        return error_run_time("failed to dump directory tree '{}': {}", opt.specified_path,
                                              status.error().message());
                    }
                }
                else
                {
                    std::ofstream file_out(opt.dump_file_path, std::ios::binary);
                    if (!file_out.is_open())
                    {
                        return error_run_time("cannot open output file '{}'", opt.dump_file_path);
                    }
                    status = store.DumpDirectoryTree(file_out, opt.specified_path, false, OutputMode::TSV,
                                                     opt.enable_dump_header);
                    if (!status)
                    {
                        return error_run_time("failed to dump directory tree to target file '{}': {}",
                                              opt.specified_path, status.error().message());
                    }
                }
            }
            break;
        default:
            return error_run_time("unknow command");
        }
        return store.GetFileStats();
    }
} // namespace ctar::cli
