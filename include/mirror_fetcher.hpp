/*
 *
 * mirror_fetcher.hpp
 *
 * Framework to fetch and store Debian mirror information.
 *
 */

#pragma once

#include <string>
#include <vector>
#include <array>
#include "structs.hpp"

class MirrorFetcher
{
public:
    static std::vector<DebianMirror> fetchMirrors();
    static std::vector<DebianMirror> getOfficialMirrors(const std::string &CountryName);
    static std::vector<DebianMirror> getAllOfficialMirrors();

private:
    static std::vector<DebianMirror> parseHtmlMirrorList(const std::string &Html);
    static size_t writeCallback(void *Contents, size_t Size, size_t NumMembers, std::string *UserP);
    static std::string extractHostname(const std::string &Url);
};

extern const std::array<CountryCode, 80> CountryCodes;
extern const std::array<OfficialMirror, 35> OfficialMirrors;