#pragma once

#include "structs.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

class MirrorFetcher {
  public:
    static std::vector<DebianMirror> fetchMirrors();
    static std::vector<DebianMirror>
    getOfficialMirrors(std::string_view CountryName = {});

  private:
    static std::vector<DebianMirror> parseHtmlMirrorList(std::string_view Html);
    static size_t writeCallback(void *Contents, size_t Size, size_t NumMembers,
                                std::string *UserStringPtr);
    static std::string extractHostname(std::string_view Url);
};

extern const std::array<CountryCode, 80> CountryCodes;
extern const std::array<OfficialMirror, 35> OfficialMirrors;
