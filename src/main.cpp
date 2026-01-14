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
    return A.TransferDelayMs < B.TransferDelayMs;
}

bool compareBySpeed(const PerformanceResult &A, const PerformanceResult &B)
{
    return A.DownloadSpeedMbps > B.DownloadSpeedMbps;
}

long myStoi(const char *flag, const char *inputStr)
{
    long long ret; // if some architecture really has weird int sizes, then let it rock baby
    auto [endpointer, errorcode] = std::from_chars(inputStr, inputStr + std::strlen(inputStr), ret);

    // Assuming the user intentionally meant "as large as possible" as a way to disable timeouts / ranking cutoffs.
    // We need to check out_of_range first for consistency between compilers.
    if (errorcode == std::errc::result_out_of_range || ret >= (long long)std::numeric_limits<long>::max())
        return kDisabledFlag;

    if (errorcode == std::errc::invalid_argument || ret == 0)
    {
        std::cerr << "Error: " << flag << " value is out of range.\n";
        std::exit(EXIT_FAILURE);
    }

    if (ret < 0)
    {
        std::cerr << "Error: " << flag << " value cannot be negative.\n";
        std::exit(EXIT_FAILURE);
    }

    return static_cast<long>(ret);
}

const std::vector<std::pair<const char*, const char*>> helpOptions = {
    {"--country <code>",
     "Filter mirrors by country. Uses ISO 3166-1 alpha-2 country codes (such as FR, DE, US, CN, etc.)"},
    {"--no-official-mirrors", "Exclude debian.org mirrors"},
    {"--only-official-mirrors", "Only test debian.org mirrors"},
    {"--count <number>", "Number of top results to display. By default, the top 5 are shown."},
    {"--timeout <milliseconds>", "How long to wait before giving up, per mirror."},
    {"--help", "This help message! For further details, please see the man page (man dmt)."},
};

void printHelp()
{
    for (const auto &opt : helpOptions)
    {
        std::cout << "  " << std::left << std::setw(32) << opt.first << opt.second << "\n";
    }
}
} // namespace

template <typename T, typename Comparator, typename MetricT>
void printTopResults(std::ostream &Out, const std::vector<T> &Source, int TopCount, Comparator Comp,
                    MetricT T::*MetricMember, std::string_view MetricUnit)
{
    std::vector<T> Temp = Source;
    std::sort(Temp.begin(), Temp.end(), Comp);

    for (size_t Idx = 0; Idx < static_cast<size_t>(TopCount) && Idx < Temp.size(); ++Idx)
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
    bool LoneStandingServer = false;
    long TopCount = 5L; // default number of "Top" results to display

    // Check argument size before parsing
    if (argc < 0)
        return 1; // defensive
    if (argc > 4096)
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
            // We'll clamp this later after fetching mirrors. So for now, just parse/validate.
            const char *countValueString = argv[++i];
            TopCount = myStoi("--count", countValueString);
        }
        else if (Arg == "--help")
        {
            printHelp();
            return 0;
        }
        else if (Arg == "--timeout" && i + 1 < argc)
        {
            const char *timeoutValueString = argv[++i];
            long TimeoutArgs = myStoi("--timeout", timeoutValueString);

            if (TimeoutArgs == kDisabledFlag)
            {
                std::cout << "Request timeouts are disabled.\n";
                PerformanceTester::RequestTimeoutMs.store(kDisabledFlag);
                continue;
            }
            else if (TimeoutArgs < 1000L)
            {
                std::cerr << "\nWARNING: a timeout of less than 1 second is not recommended.\n"
                          << "As a reminder, --timeout takes a time value measured in milliseconds.\n"
                          << "Did you definitely mean to run the program with such a short timeout? (y/n): ";
                char Response;
                std::cin >> Response;
                if (Response != 'y' && Response != 'Y')
                    std::exit(EXIT_FAILURE);
                std::cerr << "\n";
            }
            else if (TimeoutArgs < 3000L)
            {
                std::cout << "Note: A timeout of less than 3 seconds may lead to many mirrors being marked as unreachable.\n";
            }
            else
            {
                const float TimeoutSeconds = static_cast<float>(TimeoutArgs) / 1000.0f;
                std::cout << "Using request timeout of " << TimeoutSeconds << " seconds (" << TimeoutArgs << " ms)\n";
            }

            PerformanceTester::RequestTimeoutMs.store(static_cast<long>(TimeoutArgs));
        }
        else
        {
            std::cerr << "Error: Unknown option '" << Arg << "'\n";
            return 1;
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

    if (Mirrors.size() == 1)
    {
        std::cout << "It looks like there's only a single mirror available.\n";
        LoneStandingServer = true;
    }
    else
    {
        std::cout << "Fetched " << Mirrors.size() << " mirrors.\n";
    }

    // Apply filtering only if we fetched from the web (not using predefined official mirrors)
    if (!OnlyOfficial)
    {
        Mirrors = filterMirrors(Mirrors, ExcludeOfficial, OnlyOfficial, Location);
        if (CountrySpecified || ExcludeOfficial)
            std::cout << "After filtering: " << Mirrors.size() << " mirrors.\n";
    }

    // Now that we know how many mirrors we have, ensure TopCount is not some larger value.
    // If we got the disabled flag, then internally that really just means to set this value
    // to match whatever the number of mirrors we tested in total.
    if (TopCount == kDisabledFlag || static_cast<size_t>(TopCount) > Mirrors.size())
        TopCount = static_cast<long>(Mirrors.size());

    const auto StartTime = std::chrono::steady_clock::now();
    auto Results = PerformanceTester::testAllMirrors(Mirrors);
    const auto EndTime = std::chrono::steady_clock::now();
    const auto Duration = std::chrono::duration_cast<std::chrono::seconds>(EndTime - StartTime);
    std::cout << "\nTesting complete. Took " << Duration.count() << " seconds.\n";

    // If we only had a single server,
    // there is no need to continue past this point.
    if (LoneStandingServer)
    {
        return 0;
    }

    int Reachable = 0;
    double AverageNetworkDelay = 0.0;
    double AverageSpeed = 0.0;

    for (const auto &Result : Results)
    {
        if (Result.IsReachable)
        {
            Reachable++;
            AverageNetworkDelay += Result.TransferDelayMs;
            AverageSpeed += Result.DownloadSpeedMbps;
        }
    }

    const float PercentReachable =
        (Results.size() > 0) ? (static_cast<float>(Reachable) / static_cast<float>(Results.size()) * 100.0f) : 0.0f;

    std::cout << "\n" << std::string(100, '=') << "\n\n";
    std::cout << "Summary:\n";
    std::cout << "  Total mirrors tested: " << Results.size() << "\n";
    std::cout << "  Reachable mirrors: " << Reachable << " (" << PercentReachable << "%)\n";
    std::cout << "  Unreachable mirrors: " << (Results.size() - static_cast<size_t>(Reachable)) << " ("
              << (100.0f - PercentReachable) << "%)\n";

    if (Reachable > 0)
    {
        const float AvgDelayInSeconds =
            (static_cast<float>(AverageNetworkDelay) / static_cast<float>(Reachable)) / 1000.0f;
        std::cout << "\n  Average time to start transfer: " << std::fixed << std::setprecision(2)
                  << AvgDelayInSeconds << " seconds\n";
        std::cout << "  Average speed: " << std::fixed << std::setprecision(2) << (AverageSpeed / Reachable) << " Mbps\n";

        std::vector<PerformanceResult> ReachableResults;
        for (const auto &Result : Results)
        {
            if (Result.IsReachable)
                ReachableResults.push_back(Result);
        }

        const size_t DisplayCount = std::min(static_cast<size_t>(TopCount), ReachableResults.size());

        std::cout << "\n  Top " << DisplayCount << " ranked by time to start transfer:\n";
        printTopResults(std::cout, ReachableResults, static_cast<int>(DisplayCount), compareByLatency,
                        &PerformanceResult::TransferDelayMs, " ms");

        std::cout << "\n  Top " << DisplayCount << " ranked by overall download speed:\n";
        printTopResults(std::cout, ReachableResults, static_cast<int>(DisplayCount), compareBySpeed,
                        &PerformanceResult::DownloadSpeedMbps, " Mbps");
    }

    std::cout << "\n";

    return 0;
}
