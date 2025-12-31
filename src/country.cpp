// Various tools for parsing/handling country codes and names.

#include "country.hpp"
#include "mirror_fetcher.hpp"
#include <cctype>

namespace dmt
{
namespace
{
// Expected length (in characters) for ISO 3166-1 alpha-2 country codes.
constexpr std::size_t CountryCodeLengthLimit = 2;
} // namespace

std::string normalizeCountryCode(std::string_view raw)
{
    if (raw.size() < CountryCodeLengthLimit)
    {
        return "";
    }

    std::string out;
    out.reserve(CountryCodeLengthLimit);

    for (std::size_t i = 0; i < CountryCodeLengthLimit; ++i)
    {
        unsigned char uc = static_cast<unsigned char>(raw[i]);
        if (!std::isalpha(uc))
        {
            return "";
        }
        out.push_back(static_cast<char>(std::toupper(uc)));
    }

    return out;
}

std::string countryCodeToName(std::string_view code)
{
    if (code.size() != CountryCodeLengthLimit)
    {
        return "";
    }

    std::string codeUpper;
    codeUpper.reserve(CountryCodeLengthLimit);

    for (std::size_t i = 0; i < CountryCodeLengthLimit; ++i)
    {
        unsigned char uc = static_cast<unsigned char>(code[i]);
        if (!std::isalpha(uc))
        {
            return "";
        }
        codeUpper.push_back(static_cast<char>(std::toupper(uc)));
    }

    for (const auto &entry : CountryCodes)
    {
        if (cc_code(entry) == codeUpper)
        {
            return std::string(cc_name(entry));
        }
    }

    return "";
}
} // namespace dmt
