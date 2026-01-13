#include "country.hpp"
#include "mirror_fetcher.hpp"
#include "performance_tester.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

namespace
{
bool compareByLatency(const PerformanceResult &A, const PerformanceResult &B)
{
    return A.LatencyMs < B.LatencyMs;
}

bool compareBySpeed(const PerformanceResult &A, const PerformanceResult &B)
{
    return A.DownloadSpeedMbps > B.DownloadSpeedMbps;
}

bool parseClampedIntArg(const char *OptionName, int Min, int Max, const char *ArgValue, int &Out)
{
    int Parsed = 0;
    auto [Ptr, Ec] = std::from_chars(ArgValue, ArgValue + std::strlen(ArgValue), Parsed);

    if (Ec == std::errc::invalid_argument || Ec == std::errc::result_out_of_range)
    {
        std::cerr << "Error: " << OptionName << " value is out of range.\n";
        return false;
    }

    if (Parsed < Min || Parsed > Max)
    {
        return false;
    }

    Out = Parsed;
    return true;
}

std::string getHelpText()
{
    std::ostringstream Out;
    Out << R"(  --country <code>              Filter mirrors by country. Uses ISO 3166-1 alpha-2 country codes (such as FR, DE, US, CN, etc.)
  --no-official-mirrors         Exclude debian.org mirrors
  --only-official-mirrors       Only test debian.org mirrors
  --count <number>              Number of top results to display. Default is 5.
  --timeout <milliseconds>      How long to wait before giving up on each mirror. Default is )";
    Out << (static_cast<double>(PerformanceTester::DefaultTimeoutMs) / 1000.0) << " seconds ("
        << PerformanceTester::DefaultTimeoutMs << " ms).\n";
    Out << R"(  --help                        This help message!
)";
    return Out.str();
}
} // namespace

inline void printTestingDuration(std::ostream &Out, std::chrono::steady_clock::time_point StartTime,
                                 std::chrono::steady_clock::time_point EndTime)
{
    const auto Duration = std::chrono::duration_cast<std::chrono::seconds>(EndTime - StartTime);
    Out << "Testing complete. Took " << Duration.count() << " seconds.\n\n";
}

inline void printInitialResults(std::ostream &Out, std::size_t TotalTested, int Reachable)
{
    const float PercentReachable =
        (TotalTested > 0) ? (static_cast<float>(Reachable) / static_cast<float>(TotalTested) * 100.0f) : 0.0f;

    Out << std::string(100, '=') << "\n\n";
    Out << "Summary:\n";
    Out << "  Total mirrors tested: " << TotalTested << "\n";
    Out << "  Reachable mirrors: " << Reachable << " (" << PercentReachable << "%)\n";
    Out << "  Unreachable mirrors: " << (TotalTested - static_cast<std::size_t>(Reachable)) << " ("
        << (100.0f - PercentReachable) << "%)\n";
}

template <typename T, typename Comparator, typename MetricT>
inline void printTopResults(std::ostream &Out, const std::vector<T> &Source, int TopCount, Comparator Comp,
                            std::string_view HeadingText, MetricT T::*MetricMember, std::string_view MetricUnit)
{
    std::vector<T> Temp = Source;
    std::sort(Temp.begin(), Temp.end(), Comp);

    Out << "\n  Top " << static_cast<std::size_t>(TopCount) << " " << HeadingText << ":\n";
    for (std::size_t Idx = 0; Idx < static_cast<std::size_t>(TopCount) && Idx < Temp.size(); ++Idx)
    {
        const auto &Item = Temp[Idx];
        Out << "    " << (Idx + 1) << ". " << dm_url(Item.Mirror) << " - " << std::fixed << std::setprecision(2)
            << (Item.*MetricMember) << MetricUnit << "\n";
    }
}

// Filter Debian mirrors by country and official status.
// Returns a vector of mirrors matching the given filters.
std::vector<DebianMirror> filterMirrors(const std::vector<DebianMirror> &Mirrors, bool ExcludeOfficial,
                                        bool OnlyOfficial, const std::string &Location)
{
    std::vector<DebianMirror> Filtered;
    Filtered.reserve(Mirrors.size());
    std::string TargetCountry = dmt::countryCodeToName(Location);

    for (const auto &Mirror : Mirrors)
    {
        bool IsOfficial = dm_url(Mirror).find("debian.org") != std::string::npos;
        bool MatchesLocation = Location.empty() || dm_country(Mirror) == TargetCountry;

        if (!MatchesLocation)
            continue;

        if (OnlyOfficial && IsOfficial)
            Filtered.push_back(Mirror);
        else if (ExcludeOfficial && !IsOfficial)
            Filtered.push_back(Mirror);
        else if (!ExcludeOfficial && !OnlyOfficial)
            Filtered.push_back(Mirror);
    }

    return Filtered;
}

// Main entry point: parse args, fetch mirrors, run tests.
// Returns 0 on success, non-zero on failure.
int main(int argc, char *argv[])
{
    // Setup default arguments
    std::string Location;
    bool CountrySpecified = false;
    bool ExcludeOfficial = false;
    bool OnlyOfficial = false;
    int TopCount = 5; // default number of "Top" results to display
    constexpr int MaxTopCount = 300;
    constexpr int MaxTimeoutMs = 3600000; // 1 hour in milliseconds

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
            Location = dmt::normalizeCountryCode(argv[++i]);
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
            const char *NextArg = argv[++i];
            int CountArgs = 0;
            if (!parseClampedIntArg("--count", 1, MaxTopCount, NextArg, CountArgs))
                return 1;
            TopCount = CountArgs;
        }
        else if (Arg == "--help")
        {
            std::cout << getHelpText();
            return 0;
        }
        else if (Arg == "--timeout" && i + 1 < argc)
        {
            const char *NextArg = argv[++i];
            int TimeoutArgs = 0;

            if (!parseClampedIntArg("--timeout", 0, MaxTimeoutMs, NextArg, TimeoutArgs))
                return 1;
            PerformanceTester::RequestTimeoutMs.store(static_cast<long>(TimeoutArgs));

            if (TimeoutArgs < 1000)
                std::cerr << "WARNING: a timeout of less than 1 second is not recommended.\n\n";

            const float TimeoutSeconds = static_cast<float>(TimeoutArgs) / 1000.0f;
            std::cout << "Using request timeout of " << TimeoutSeconds << " seconds (" << TimeoutArgs << " ms)\n";
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
        std::string TargetCountry = dmt::countryCodeToName(Location);
        if (TargetCountry.empty())
        {
            std::cerr << "Error: Invalid country code '" << Location << "'\n";
            return 1;
        }
    }

    // If only-official-mirrors is specified, use predefined official mirrors to
    // avoid a web fetch
    std::vector<DebianMirror> Mirrors;
    if (OnlyOfficial)
    {
        if (!Location.empty())
        {
            std::string TargetCountry = dmt::countryCodeToName(Location);
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
            Mirrors = MirrorFetcher::getOfficialMirrors();
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

    // Apply filtering only if we fetched from the web (not using predefined
    // official mirrors)
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
    double AverageNetworkDelay = 0.0;
    double AvgSpeed = 0.0;

    for (const auto &Result : Results)
    {
        if (Result.IsReachable)
        {
            Reachable++;
            AverageNetworkDelay += Result.TransferDelayMs;
            AvgSpeed += Result.DownloadSpeedMbps;
        }
    }

    printInitialResults(std::cout, Results.size(), Reachable);

    if (Reachable > 0)
    {
        std::cout << "\n  Average time to start transfer: " << std::fixed << std::setprecision(2)
                  << (AvgLatency / Reachable) << " ms\n";
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
        printTopResults(std::cout, ReachableResults, TopCount, compareByLatency, "ranked by time to start transfer",
                        &PerformanceResult::TransferDelayMs, " ms");

        // Top results by speed (highest first)
        printTopResults(std::cout, ReachableResults, TopCount, compareBySpeed, "ranked by overall download speed",
                        &PerformanceResult::DownloadSpeedMbps, " Mbps");
    }

    std::cout << "\n";

    return 0;
}
