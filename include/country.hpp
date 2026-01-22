#pragma once

#include <string>
#include <string_view>

namespace CountryCodes {
std::string normalize(std::string_view raw);
std::string getName(std::string_view code);
} // namespace CountryCodes
