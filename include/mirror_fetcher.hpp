#pragma once

#include "structs.hpp"
#include <array>
#include <string>
#include <vector>

class MirrorFetcher
{
  public:
    static std::vector<DebianMirror> fetchMirrors();
    static std::vector<DebianMirror> getOfficialMirrors(const std::string &CountryName = "");

  private:
    static std::vector<DebianMirror> parseHtmlMirrorList(const std::string &Html);
    static size_t writeCallback(void *Contents, size_t Size, size_t NumMembers, std::string *UserP);
    static std::string extractHostname(const std::string &Url);
};

extern const std::array<CountryCode, 80> CountryCodes;

// Official Debian mirrors. these urls are static and do not need to be fetched
// from the web.
extern const std::array<OfficialMirror, 35> OfficialMirrors;
