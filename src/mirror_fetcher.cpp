#include "mirror_fetcher.hpp"
#include <algorithm>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

// libcurl write callback — append received data to the provided std::string.
// Returns number of bytes written (Size * NumMembers).
size_t MirrorFetcher::writeCallback(void *Contents, size_t Size, size_t NumMembers, std::string *UserP)
{
    UserP->append((char *)Contents, Size * NumMembers);
    return Size * NumMembers;
}

// Fetch Debian mirror listing pages and parse discovered mirrors.
// Tries multiple canonical URLs; returns empty vector if all attempts fail.
std::vector<DebianMirror> MirrorFetcher::fetchMirrors()
{
    CURL *Curl = curl_easy_init();
    if (!Curl)
    {
        std::cerr << "Failed to initialize CURL\n";
        return {};
    }

    std::string Response;
    const char *Urls[] = {"https://www.debian.org/mirror/list", "https://www.debian.org/mirror/mirrors_full",
                          "https://www.debian.org/mirror/official"};
    const int NumUrls = sizeof(Urls) / sizeof(Urls[0]);

    curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &Response);
    curl_easy_setopt(Curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode Res = CURLE_OK;
    for (int Idx = 0; Idx < NumUrls; ++Idx)
    {
        Response.clear();
        curl_easy_setopt(Curl, CURLOPT_URL, Urls[Idx]);

        Res = curl_easy_perform(Curl);
        if (Res == CURLE_OK)
        {
            std::cerr << "Successfully fetched mirrors from: " << Urls[Idx] << "\n";
            curl_easy_cleanup(Curl);
            return parseHtmlMirrorList(Response);
        }

        std::cerr << "Attempt " << (Idx + 1) << " failed (" << Urls[Idx] << "): " << curl_easy_strerror(Res) << "\n";
    }

    std::cerr << "All mirror fetch attempts failed\n";
    curl_easy_cleanup(Curl);
    return {};
}

// Parse Debian mirror-listing HTML and extract mirrors (URL + country).
// Associates country headers with nearby URLs, filters for "/debian", and
// de-duplicates.
std::vector<DebianMirror> MirrorFetcher::parseHtmlMirrorList(std::string_view Html)
{
    std::vector<DebianMirror> Mirrors;
    std::string LastCountry = "Unknown";

    const char *pattern = "href=\"(https?://[^\"]+)\"";
    std::regex UrlPattern(pattern);

    // common patterns: <h2>CountryName</h2>, <strong>CountryName</strong>, etc
    const char *country_pattern = "<(?:h[2-3]|strong|b)>([A-Z][a-z]+(?:\\s[A-Z]"
                                  "[a-z]+)*)</(?:h[2-3]|strong|b)>";
    std::regex CountryRegex(country_pattern);

    std::cregex_iterator UrlIt(Html.data(), Html.data() + Html.size(), UrlPattern);
    std::cregex_iterator UrlEnd;
    std::cregex_iterator CountryIt(Html.data(), Html.data() + Html.size(), CountryRegex);
    std::cregex_iterator CountryEnd;

    // Build a map of position -> country name
    // The size_t is the position in the HTML string, and the std::string is the
    // country name
    std::vector<std::pair<size_t, std::string>> CountryPositions;
    while (CountryIt != CountryEnd)
    {
        CountryPositions.push_back({CountryIt->position(0), CountryIt->str(1)});
        ++CountryIt;
    }

    // Process each URL
    UrlIt = std::cregex_iterator(Html.data(), Html.data() + Html.size(), UrlPattern);
    while (UrlIt != UrlEnd)
    {
        auto Match = *UrlIt;
        std::string Url = Match[1].str();
        size_t UrlPos = Match.position(0);

        if (Url.find("/debian") != std::string::npos)
        {
            // Keep track of the most recent country header before this URL
            for (const auto &CountryPair : CountryPositions)
            {
                if (CountryPair.first < UrlPos)
                    LastCountry = CountryPair.second;
                else
                    break;
            }

            Mirrors.push_back({Url, LastCountry});
        }

        ++UrlIt;
    }

    // Remove duplicates while preserving order
    std::vector<DebianMirror> UniqueMirrors;
    std::vector<std::string> SeenUrls;

    for (const auto &Mirror : Mirrors)
    {
        if (std::find(SeenUrls.begin(), SeenUrls.end(), dm_url(Mirror)) == SeenUrls.end())
        {
            SeenUrls.push_back(dm_url(Mirror));
            UniqueMirrors.push_back(Mirror);
        }
    }

    return UniqueMirrors;
}

// Strips an optional scheme ("http://" or "https://") and returns characters up
// to the next '/'. Returns an empty string when the input is empty or does not
// contain a hostname.
std::string MirrorFetcher::extractHostname(std::string_view Url)
{
    size_t Start = Url.find("://");
    if (Start != std::string_view::npos)
        Start += 3;
    else
        Start = 0;

    size_t End = Url.find('/', Start);
    if (End == std::string_view::npos)
        End = Url.length();

    return std::string(Url.substr(Start, End - Start));
}

// Return a vector containing official mirrors.
// If CountryName is provided, returns mirrors for that country only.
// If CountryName is empty, returns all official mirrors.
std::vector<DebianMirror> MirrorFetcher::getOfficialMirrors(std::string_view CountryName)
{
    std::vector<DebianMirror> Mirrors;

    for (const auto &official : OfficialMirrors)
    {
        if (CountryName.empty() || om_country(official) == CountryName)
            Mirrors.push_back({std::string(om_url(official)), std::string(om_country(official))});
    }

    return Mirrors;
}

const std::array<CountryCode, 80> CountryCodes = {{{"Argentina", "AR"},
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

// these urls are static and do not need to be fetched from the web.
const std::array<OfficialMirror, 35> OfficialMirrors = {
    {{"Australia", "ftp://ftp.au.debian.org/debian/"},    {"Austria", "ftp://ftp.at.debian.org/debian/"},
     {"Belgium", "ftp://ftp.be.debian.org/debian/"},      {"Brazil", "ftp://ftp.br.debian.org/debian/"},
     {"Bulgaria", "ftp://ftp.bg.debian.org/debian/"},     {"Canada", "ftp://ftp.ca.debian.org/debian/"},
     {"Chile", "ftp://ftp.cl.debian.org/debian/"},        {"China", "ftp://ftp.cn.debian.org/debian/"},
     {"Croatia", "ftp://ftp.hr.debian.org/debian/"},      {"Czech Republic", "ftp://ftp.cz.debian.org/debian/"},
     {"Denmark", "ftp://ftp.dk.debian.org/debian/"},      {"Finland", "ftp://ftp.fi.debian.org/debian/"},
     {"France", "ftp://ftp.fr.debian.org/debian/"},       {"Germany", "ftp://ftp.de.debian.org/debian/"},
     {"Germany", "ftp://ftp2.de.debian.org/debian/"},     {"Hong Kong", "ftp://ftp.hk.debian.org/debian/"},
     {"Iceland", "ftp://ftp.is.debian.org/debian/"},      {"Italy", "ftp://ftp.it.debian.org/debian/"},
     {"Japan", "ftp://ftp.jp.debian.org/debian/"},        {"Lithuania", "ftp://ftp.lt.debian.org/debian/"},
     {"Netherlands", "ftp://ftp.nl.debian.org/debian/"},  {"New Caledonia", "ftp://ftp.nc.debian.org/debian/"},
     {"New Zealand", "ftp://ftp.nz.debian.org/debian/"},  {"Norway", "ftp://ftp.no.debian.org/debian/"},
     {"Poland", "ftp://ftp.pl.debian.org/debian/"},       {"Portugal", "ftp://ftp.pt.debian.org/debian/"},
     {"Russia", "ftp://ftp.ru.debian.org/debian/"},       {"Slovakia", "ftp://ftp.sk.debian.org/debian/"},
     {"Slovenia", "ftp://ftp.si.debian.org/debian/"},     {"Spain", "ftp://ftp.es.debian.org/debian/"},
     {"Sweden", "ftp://ftp.se.debian.org/debian/"},       {"Switzerland", "ftp://ftp.ch.debian.org/debian/"},
     {"Taiwan", "ftp://ftp.tw.debian.org/debian/"},       {"United Kingdom", "ftp://ftp.uk.debian.org/debian/"},
     {"United States", "ftp://ftp.us.debian.org/debian/"}}};