#include <iostream>
#include "arg_parser.h"
#include "src/utils/tools.h"

int main(int argc, char** argv)
{
    ctar::ScopeTimer scope_timer("", false);
    ctar::cli::CliOption opt{};
    if (auto s = ctar::cli::ParseArgs(argc, argv, opt); !s)
    {
        std::println("{}", s.error().err_message);
        ctar::cli::PrintHelp();
        return EXIT_FAILURE;
    }
    auto status = ctar::cli::RunCommand(opt);
    if (status)
    {
        if (opt.cmd == ctar::cli::CommandType::PACK)
        {
            auto file_stats = status.value();
            auto elapsed_time = scope_timer.elapsed();
            std::println(std::cout,
                         "file count: {}, total bytes: {}, compressed bytes: {}, padding bytes: {}, elapsed time {} ms",
                         file_stats.total_file_count, file_stats.total_original_size, file_stats.total_compressed_size,
                         file_stats.padding_size, elapsed_time);
            std::println("compressed ratio: {:.2f}%, Throughput: {}/s",
                         ctar::CompressionRatio(file_stats.total_original_size, file_stats.total_compressed_size),
                         ctar::ThroughputPerSec(file_stats.total_original_size, elapsed_time));
        }
        else if (opt.cmd == ctar::cli::CommandType::STAT)
        {
            auto file_stats = status.value();
            std::println(std::cout, "file count: {}, total bytes: {}, compressed bytes: {}, padding bytes: {}",
                         file_stats.total_file_count, file_stats.total_original_size, file_stats.total_compressed_size,
                         file_stats.padding_size);
        }
        return EXIT_SUCCESS;
    }
    std::println(std::cerr, "{}", status.error().message());
    return EXIT_FAILURE;
}
