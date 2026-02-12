#include "country.hpp"
#include "mirror_fetcher.hpp"
#include <cctype>

namespace CountryCodes {
// Expected length (in characters) for ISO 3166-1 alpha-2 country codes.
constexpr size_t ISO_ALPHA2_SIZE = 2;
constexpr std::array<CountryCode, 80> CountryCodeList = {
    {{"Argentina", "AR"},
     {"Australia", "AU"},
     {"Austria", "AT"},
     {"Azerbaijan", "AZ"},
     {"Bangladesh", "BD"},
     {"Belarus", "BY"},
     {"Belgium", "BE"},
     {"Brazil", "BR"},
     {"Bulgaria", "BG"},
     {"Burkina Faso", "BF"},
     {"Canada", "CA"},
     {"Chile", "CL"},
     {"China", "CN"},
     {"Colombia", "CO"},
     {"Costa Rica", "CR"},
     {"Croatia", "HR"},
     {"Czech Republic", "CZ"},
     {"Denmark", "DK"},
     {"Dominican Republic", "DO"},
     {"Ecuador", "EC"},
     {"Finland", "FI"},
     {"France", "FR"},
     {"Germany", "DE"},
     {"Greece", "GR"},
     {"Hong Kong", "HK"},
     {"Hungary", "HU"},
     {"Iceland", "IS"},
     {"India", "IN"},
     {"Indonesia", "ID"},
     {"Iran", "IR"},
     {"Ireland", "IE"},
     {"Israel", "IL"},
     {"Italy", "IT"},
     {"Japan", "JP"},
     {"Kazakhstan", "KZ"},
     {"Kenya", "KE"},
     {"Latvia", "LV"},
     {"Lithuania", "LT"},
     {"Luxembourg", "LU"},
     {"Malaysia", "MY"},
     {"Mexico", "MX"},
     {"Morocco", "MA"},
     {"Netherlands", "NL"},
     {"New Caledonia", "NC"},
     {"New Zealand", "NZ"},
     {"Nicaragua", "NI"},
     {"Norway", "NO"},
     {"Pakistan", "PK"},
     {"Panama", "PA"},
     {"Paraguay", "PY"},
     {"Peru", "PE"},
     {"Philippines", "PH"},
     {"Poland", "PL"},
     {"Portugal", "PT"},
     {"Romania", "RO"},
     {"Russia", "RU"},
     {"Saudi Arabia", "SA"},
     {"Serbia", "RS"},
     {"Singapore", "SG"},
     {"Slovakia", "SK"},
     {"Slovenia", "SI"},
     {"South Africa", "ZA"},
     {"South Korea", "KR"},
     {"Spain", "ES"},
     {"Sri Lanka", "LK"},
     {"Sweden", "SE"},
     {"Switzerland", "CH"},
     {"Taiwan", "TW"},
     {"Thailand", "TH"},
     {"Turkey", "TR"},
     {"Ukraine", "UA"},
     {"United Kingdom", "GB"},
     {"United States", "US"},
     {"Uruguay", "UY"},
     {"Venezuela", "VE"},
     {"Vietnam", "VN"}}};

// Input: raw country code (any case).
// Output: uppercase, two-character country code.
// Returns an empty string if invalid.
std::string normalize(std::string_view raw) {
    if (raw.size() < ISO_ALPHA2_SIZE)
        return "";

    std::string out;
    out.reserve(ISO_ALPHA2_SIZE);

    for (size_t i = 0; i < ISO_ALPHA2_SIZE; ++i) {
        unsigned char uc = static_cast<unsigned char>(raw[i]);
        if (!std::isalpha(uc))
            return "";
        out.push_back(static_cast<char>(std::toupper(uc)));
    }

    return out;
}

// Input: country code (ISO 3166-1 alpha-2, uppercase or lowercase).
// Output: Full country name as a string.
// Returns an empty string if no match is found.
std::string getName(std::string_view code) {
    if (code.size() != ISO_ALPHA2_SIZE)
        return "";

    std::string codeUpper;
    codeUpper.reserve(ISO_ALPHA2_SIZE);

    for (size_t i = 0; i < ISO_ALPHA2_SIZE; ++i) {
        unsigned char uc = static_cast<unsigned char>(code[i]);
        if (!std::isalpha(uc))
            return "";
        codeUpper.push_back(static_cast<char>(std::toupper(uc)));
    }

    for (const auto &entry : CountryCodes::CountryCodeList) {
        if (cc_code(entry) == codeUpper)
            return std::string(cc_name(entry));
    }

    return "";
}
} // namespace CountryCodes
