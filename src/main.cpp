#include "main.hpp"
#include "mirror_fetcher.hpp"
#include "performance_tester.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <string>
#include <cctype>
#include <charconv>
#include <cstring> // GCC won't build without this here for strlen, though Clang doesn't require it.

// Default number of top mirrors to display when printing results. May be overridden by --count.
static int DefaultNumTopEntries = 5;

// Expected length (in characters) for ISO 3166-1 alpha-2 country codes.
static size_t CountryCodeLengthLimit = 2;

static bool compareByLatency(const PerformanceResult &A, const PerformanceResult &B)
{
    return A.LatencyMs < B.LatencyMs;
}

static bool compareBySpeed(const PerformanceResult &A, const PerformanceResult &B)
{
    return A.DownloadSpeedMbps > B.DownloadSpeedMbps;
}

inline bool parsePositiveUInt16(const char *Value, int &Parsed, std::ostream &Err)
{
    Parsed = 0;
    char *EndPtr = nullptr;
    long ParsedLong = std::strtol(Value, &EndPtr, 10);
    
    if (EndPtr == Value || *EndPtr != '\0')
    {
        Err << "Value must be a valid integer!\n";
        return false;
    }

    Parsed = static_cast<int>(ParsedLong);
    if (Parsed <= 0 || Parsed > static_cast<int>(std::numeric_limits<std::uint16_t>::max()))
    {
        Err << "Value must be between 1 and 65535!\n";
        return false;
    }

    return true;
}

inline void printTestingDuration(std::ostream &Out,
                                 std::chrono::steady_clock::time_point StartTime,
                                 std::chrono::steady_clock::time_point EndTime)
{
    const auto Duration = std::chrono::duration_cast<std::chrono::seconds>(EndTime - StartTime);
    Out << "Testing complete. Took " << Duration.count() << " seconds.\n\n";
}

inline void printInitialResults(std::ostream &Out, std::size_t TotalTested, int Reachable)
{
    const float PercentReachable = (TotalTested > 0)
                                       ? (static_cast<float>(Reachable) / static_cast<float>(TotalTested) * 100.0f)
                                       : 0.0f;

    Out << std::string(100, '=') << "\n\n";
    Out << "Summary:\n";
    Out << "  Total mirrors tested: " << TotalTested << "\n";
    Out << "  Reachable mirrors: " << Reachable << " (" << PercentReachable << "%)\n";
    Out << "  Unreachable mirrors: " << (TotalTested - static_cast<std::size_t>(Reachable))
        << " (" << (100.0f - PercentReachable) << "%)\n";
}

template <typename T, typename Comparator, typename MetricT>
inline void printTopResults(std::ostream &Out,
                            const std::vector<T> &Source,
                            int TopCount,
                            Comparator Comp,
                            std::string_view HeadingText,
                            MetricT T::*MetricMember,
                            std::string_view MetricUnit)
{
    std::vector<T> Temp = Source;
    std::sort(Temp.begin(), Temp.end(), Comp);

    Out << "\n  Top " << static_cast<std::size_t>(TopCount) << " " << HeadingText << ":\n";
    for (std::size_t Idx = 0; Idx < static_cast<std::size_t>(TopCount) && Idx < Temp.size(); ++Idx)
    {
        const auto &Item = Temp[Idx];
        Out << "    " << (Idx + 1) << ". " << Item.Mirror.Url << " - "
            << std::fixed << std::setprecision(2) << (Item.*MetricMember) << MetricUnit << "\n";
    }
}

/*
 Summary:
   Truncate at the first two characters and convert to uppercase to form a
   (hopefully) valid ISO 3166-1 alpha-2 country code.

 Behavior:
   - If we don't have at least two characters, we return an empty string.
   - Otherwise, takes the first CountryCodeLengthLimit characters, verifies each
     is an alphabetic ASCII character, converts them to uppercase, and returns them.
   - Any non-alphabetic character in those positions yields an empty string.

 Examples:
   normalizeCountryCode("us")     -> "US"
   normalizeCountryCode("United") -> "UN"
   normalizeCountryCode("U1")     -> ""   // invalid, contains non-letter
*/
static std::string normalizeCountryCode(const std::string &Raw)
{
    if (Raw.size() < CountryCodeLengthLimit)
    {
        return "";
    }

    std::string Out = Raw.substr(0, CountryCodeLengthLimit);
    for (char &C : Out)
    {
        unsigned char UC = static_cast<unsigned char>(C);
        if (!std::isalpha(UC))
        {
            return "";
        }
        C = static_cast<char>(std::toupper(UC));
    }

    return Out;
}

/*
 Summary:
   Convert an ISO 3166-1 alpha-2 code (exactly CountryCodeLengthLimit letters) to
   a human-readable country name.

 Examples:
   countryCodeToName("US") -> "United States"
   countryCodeToName("zz") -> "" // unknown code
 */
static std::string countryCodeToName(const std::string &Code)
{
    if (Code.length() != CountryCodeLengthLimit)
    {
        return "";
    }

    std::string CodeUpper = Code;

    for (char &C : CodeUpper)
    {
        if (!std::isalpha(static_cast<unsigned char>(C)))
        {
            return "";
        }
        C = std::toupper(static_cast<unsigned char>(C));
    }

    for (const auto &Entry : CountryCodes)
    {
        if (Entry.Code && std::string(Entry.Code) == CodeUpper)
        {
            return Entry.Name ? Entry.Name : "";
        }
    }
    return "";
}

/*
 Summary:
   Filter a list of Debian mirrors by country and official status.

 Parameters:
   mirrors          - Source vector of DebianMirror entries.
   exclude_official - Flag to decide whether to include only non-official mirrors.
   only_official    - Flag to decide whether to include only official mirrors.
   location         - ISO 3166-1 alpha-2 country code of the desired location.

 Returns:
   Vector of mirrors matching the requested filters.
 */
std::vector<DebianMirror> filterMirrors(
    const std::vector<DebianMirror> &Mirrors,
    bool ExcludeOfficial,
    bool OnlyOfficial,
    const std::string &Location)
{

    std::vector<DebianMirror> Filtered;
    Filtered.reserve(Mirrors.size());
    std::string TargetCountry = countryCodeToName(Location);

    for (const auto &Mirror : Mirrors)
    {
        bool IsOfficial = Mirror.Url.find("debian.org") != std::string::npos;
        bool MatchesLocation = Location.empty() || Mirror.Country == TargetCountry;

        if (!MatchesLocation)
        {
            continue;
        }

        if (OnlyOfficial && IsOfficial)
        {
            Filtered.push_back(Mirror);
        }
        else if (ExcludeOfficial && !IsOfficial)
        {
            Filtered.push_back(Mirror);
        }
        else if (!ExcludeOfficial && !OnlyOfficial)
        {
            Filtered.push_back(Mirror);
        }
    }

    return Filtered; // RVO
}

/*
 Summary:
   Main entry point for the program.

 Behavior:
    - Parses command-line arguments to configure filtering and testing options.
    - Fetches the list of Debian mirrors, applying filters as specified.
    - Runs performance tests on the selected mirrors and displays results.

Returns:
    - 0 on success, non-zero on failure.
*/
int main(int argc, char *argv[])
{
    // Setup default arguments
    std::string Location;
    bool CountrySpecified = false;
    bool ExcludeOfficial = false;
    bool OnlyOfficial = false;
    int TopCount = DefaultNumTopEntries;

    // Check argument size before parsing
    constexpr int MaxArgs = 4096;
    if (argc < 0)
        return 1; // defensive
    if (argc > MaxArgs)
    {
        std::cerr << "Error: too many arguments\n";
        return 1;
    }

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string Arg = argv[i];
        if (Arg == "--country" && i + 1 < argc)
        {
            CountrySpecified = true;
            Location = normalizeCountryCode(argv[++i]);
        }
        else if (Arg == "--no-official-mirrors")
        {
            ExcludeOfficial = true;
        }
        else if (Arg == "--only-official-mirrors")
        {
            OnlyOfficial = true;
        }
        else if (Arg == "--count" && i + 1 < argc)
        {
            int Parsed = 0;
            if (!parsePositiveUInt16(argv[++i], Parsed, std::cerr))
            {
                return 1;
            }
            TopCount = Parsed;
        }
        else if (Arg == "--help")
        {
            std::cout << HelpText;
            return 0;
        }
        else if (Arg == "--timeout" && i + 1 < argc)
        {
            int Parsed = 0;
            if (!parsePositiveUInt16(argv[++i], Parsed, std::cerr))
            {
                return 1;
            }
            PerformanceTester::RequestTimeoutMs.store(static_cast<long>(Parsed));

            if (Parsed < 1000)
            {
                std::cerr << "WARNING: a timeout of less than 1 second is not recommended.\n\n";
            }

            std::cout << "Using request timeout of " << Parsed << " ms\n";
        }
    }

    // Validate that --country was given a valid code
    if (CountrySpecified && Location.empty())
    {
        std::cerr << "Error: --country requires a valid ISO 3166-1 alpha-2 country code\n";
        return 1;
    }

    // Validate country code
    if (!Location.empty())
    {
        std::string TargetCountry = countryCodeToName(Location);
        if (TargetCountry.empty())
        {
            std::cerr << "Error: Invalid country code '" << Location << "'\n";
            return 1;
        }
    }

    // If only-official-mirrors is specified, use predefined official mirrors to avoid a web fetch
    std::vector<DebianMirror> Mirrors;
    if (OnlyOfficial)
    {
        if (!Location.empty())
        {
            std::string TargetCountry = countryCodeToName(Location);
            if (!TargetCountry.empty())
            {
                std::cout << "Using official mirrors for " << TargetCountry << "...\n";
                Mirrors = MirrorFetcher::getOfficialMirrors(TargetCountry);
            }
            else
            {
                std::cerr << "Error: Invalid country code '" << Location << "'\n";
                return 1;
            }
        }
        else
        {
            std::cout << "Using all official Debian mirrors...\n";
            Mirrors = MirrorFetcher::getAllOfficialMirrors();
        }
    }
    else
    {
        std::cout << "Fetching mirror list...\n";
        Mirrors = MirrorFetcher::fetchMirrors();
    }

    if (Mirrors.empty())
    {
        std::cerr << "Error: Failed to fetch mirrors\n";
        return 1;
    }

    std::cout << "Found " << Mirrors.size() << " mirrors.\n";

    // Apply filtering only if we fetched from the web (not using predefined official mirrors)
    if (!OnlyOfficial)
    {
        Mirrors = filterMirrors(Mirrors, ExcludeOfficial, OnlyOfficial, Location);
        if (CountrySpecified || ExcludeOfficial)
        {
            std::cout << "After filtering: " << Mirrors.size() << " mirrors.\n";
        }
    }

    const auto StartTime = std::chrono::steady_clock::now();
    auto Results = PerformanceTester::testAllMirrors(Mirrors);
    const auto EndTime = std::chrono::steady_clock::now();
    printTestingDuration(std::cout, StartTime, EndTime);

    int Reachable = 0;
    double AvgLatency = 0.0;
    double AvgSpeed = 0.0;

    for (const auto &Result : Results)
    {
        if (Result.IsReachable)
        {
            Reachable++;
            AvgLatency += Result.LatencyMs;
            AvgSpeed += Result.DownloadSpeedMbps;
        }
    }

    printInitialResults(std::cout, Results.size(), Reachable);

    if (Reachable > 0)
    {
        std::cout << "\n  Average time to start transfer: " << std::fixed << std::setprecision(2) << (AvgLatency / Reachable) << " ms\n";
        std::cout << "  Average speed: " << std::fixed << std::setprecision(2) << (AvgSpeed / Reachable) << " Mbps\n";

        // Filter reachable results
        std::vector<PerformanceResult> ReachableResults;
        for (const auto &Result : Results)
        {
            if (Result.IsReachable)
            {
                ReachableResults.push_back(Result);
            }
        }

        // Top results by latency (lowest first)
        printTopResults(
            std::cout,
            ReachableResults,
            TopCount,
            compareByLatency,
            "ranked by time to start transfer",
            &PerformanceResult::LatencyMs,
            " ms");

        // Top results by speed (highest first)
        printTopResults(
            std::cout,
            ReachableResults,
            TopCount,
            compareBySpeed,
            "ranked by overall download speed",
            &PerformanceResult::DownloadSpeedMbps,
            " Mbps");
    }

    std::cout << "\n";

    return 0;
}
