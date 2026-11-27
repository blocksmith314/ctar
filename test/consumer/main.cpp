#include <ctar/ctar.h>

#include <iostream>
#include <string>

int main()
{
    // VersionString() is defined in libctar, so this only links when the
    // installed archive is actually usable.
    const std::string version = ctar::VersionString();
    if (version != ctar::kToolVersion.to_string())
    {
        std::cerr << "version mismatch: library " << version << ", headers " << ctar::kToolVersion.to_string()
                  << '\n';
        return 1;
    }
    std::cout << "ctar " << version << " (format " << ctar::kFileFormatVersion.to_string() << ")\n";
    return 0;
}
