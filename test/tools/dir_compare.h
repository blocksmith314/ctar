#ifndef CTAR_DIR_COMPARE_H
#define CTAR_DIR_COMPARE_H
#include <filesystem>

namespace ctar::test
{
    bool AreDirectoriesEqual(const std::filesystem::path& dir1, const std::filesystem::path& dir2);
}

#endif // CTAR_DIR_COMPARE_H
