#pragma once

#include <string>
#include <string_view>

namespace dmt
{
std::string normalizeCountryCode(std::string_view raw);
std::string countryCodeToName(std::string_view code);
} // namespace dmt
