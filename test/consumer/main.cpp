#include <ctar/ctar.h>

#include <iostream>
#include <string>
#include <string_view>

namespace
{
    int ReadArchive(const char* archive_path)
    {
        auto reader = ctar::ArchiveReader::Open(archive_path);
        if (!reader)
        {
            std::cerr << "open failed: " << reader.error().message << '\n';
            return 1;
        }

        auto entries = reader->List("/", true);
        if (!entries)
        {
            std::cerr << "list failed: " << entries.error().message << '\n';
            return 1;
        }

        const ctar::Entry* hello = nullptr;
        for (const auto& entry : entries.value())
        {
            if (entry.path == "/hello.txt")
            {
                hello = &entry;
            }
        }
        if (hello == nullptr)
        {
            std::cerr << "hello.txt is missing from the archive\n";
            return 1;
        }

        auto data = reader->ReadFile("/hello.txt");
        if (!data)
        {
            std::cerr << "read failed: " << data.error().message << '\n';
            return 1;
        }
        if (data.value()->view() != "hello\n")
        {
            std::cerr << "unexpected file contents\n";
            return 1;
        }

        std::cout << "read " << data.value()->size() << " bytes from " << entries->size() << " archive entries\n";
        return 0;
    }
} // namespace

int main(int argc, char** argv)
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

    if (argc > 1)
    {
        return ReadArchive(argv[1]);
    }
    return 0;
}
