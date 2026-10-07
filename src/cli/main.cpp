#include <iostream>
#include "arg_parser.h"

int main(int argc, char** argv)
{
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
        return EXIT_SUCCESS;
    }
    std::println(std::cerr, "{}", status.error().message());
    return EXIT_FAILURE;
}
