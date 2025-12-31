#pragma once

#include <string>
#include <string_view>

struct CountryCode
{
    std::string_view name;
    std::string_view code;
};
inline constexpr std::string_view cc_name(const CountryCode &cc) noexcept
{
    return cc.name;
}
inline constexpr std::string_view cc_code(const CountryCode &cc) noexcept
{
    return cc.code;
}

struct OfficialMirror
{
    std::string_view country;
    std::string_view url;
};
inline constexpr std::string_view om_country(const OfficialMirror &om) noexcept
{
    return om.country;
}
inline constexpr std::string_view om_url(const OfficialMirror &om) noexcept
{
    return om.url;
}

struct DebianMirror
{
    std::string url;
    std::string country;
};
inline const std::string &dm_url(const DebianMirror &dm) noexcept
{
    return dm.url;
}
inline const std::string &dm_country(const DebianMirror &dm) noexcept
{
    return dm.country;
}
