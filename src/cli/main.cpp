#include <exception>
#include <iostream>
#include "arg_parser.h"

int main(int argc, char** argv)
{
    try
    {
        ctar::cli::CliOption opt{};
        if (auto s = ctar::cli::ParseArgs(argc, argv, opt); !s)
        {
            std::println(std::cout, "{}", s.error().err_message);
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
    catch (const std::exception& e)
    {
        // Last line of defence: malformed input must never take the process down.
        std::println(std::cerr, "ctar: {}", e.what());
        return EXIT_FAILURE;
    }
}
