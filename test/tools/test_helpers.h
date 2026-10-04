#ifndef CTAR_TEST_HELPER_H
#define CTAR_TEST_HELPER_H

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ctar::test
{
    class CoutRedirectGuard
    {
    public:
        explicit CoutRedirectGuard(std::ostringstream& buffer) : old_buf_(std::cout.rdbuf(buffer.rdbuf())) {}

        ~CoutRedirectGuard() { std::cout.rdbuf(old_buf_); }

        CoutRedirectGuard(const CoutRedirectGuard&) = delete;
        CoutRedirectGuard& operator=(const CoutRedirectGuard&) = delete;

    private:
        std::streambuf* old_buf_;
    };

    class CerrRedirectGuard
    {
    public:
        explicit CerrRedirectGuard(std::ostringstream& buffer) : old_buf_(std::cerr.rdbuf(buffer.rdbuf())) {}

        ~CerrRedirectGuard() { std::cerr.rdbuf(old_buf_); }

        CerrRedirectGuard(const CerrRedirectGuard&) = delete;
        CerrRedirectGuard& operator=(const CerrRedirectGuard&) = delete;

    private:
        std::streambuf* old_buf_;
    };

    inline bool StringContainsAll(const std::string& text, const std::vector<std::string>& keywords)
    {
        for (const auto& kw : keywords)
        {
            if (text.find(kw) == std::string::npos)
            {
                return false;
            }
        }
        return true;
    }

    inline bool ContainsElement(const std::vector<std::string>& vec, const std::string& target)
    {
        return std::ranges::contains(vec, target);
    }

    inline std::vector<std::string_view> SplitView(std::string_view s, char delimiter)
    {
        std::vector<std::string_view> res;
        size_t start = 0;
        size_t pos;
        while ((pos = s.find(delimiter, start)) != std::string_view::npos)
        {
            res.push_back(s.substr(start, pos - start));
            start = pos + 1;
        }
        res.push_back(s.substr(start));
        return res;
    }
} // namespace ctar::test

#endif // CTAR_TEST_HELPER_H