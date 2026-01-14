#include "country.hpp"
#include "mirror_fetcher.hpp"
#include "performance_tester.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
auto compareByLatency = [](const PerformanceResult &A, const PerformanceResult &B) {
    return A.TransferDelayMs < B.TransferDelayMs;
};
auto compareBySpeed = [](const PerformanceResult &A, const PerformanceResult &B) {
    return A.DownloadSpeedMbps > B.DownloadSpeedMbps;
};

long myStoi(const char *flag, const char *inputStr)
{
    long long ret;
    auto [_, ec] = std::from_chars(inputStr, inputStr + std::strlen(inputStr), ret);
    if (ec == std::errc::result_out_of_range || ret > (long long)std::numeric_limits<long>::max())
        return kDisabledFlag;
    if (ec == std::errc::invalid_argument || ret == 0)
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

std::string howdy()
{
    std::ostringstream O;
    O << R"(  --country <code>              Filter mirrors by country. Uses ISO 3166-1 alpha-2 country codes (such as FR, DE, US, CN, etc.)
  --no-official-mirrors         Exclude debian.org mirrors
  --only-official-mirrors       Only test debian.org mirrors
  --count <number>              Number of top results to display. Default is 5.
  --timeout <milliseconds>      How long to wait before giving up on each mirror. Default is )"
      << (PerformanceTester::DefaultTimeoutMs / 1000.0) << " seconds (" << PerformanceTester::DefaultTimeoutMs
      << " ms).\n"
      << R"(  --help                        This help message! For further details, please see the man page (man dmt).)";
    return O.str();
}
} // namespace

inline void printTestingDuration(std::ostream &Out, std::chrono::steady_clock::time_point StartTime,
                                 std::chrono::steady_clock::time_point EndTime)
{
    Out << "Testing complete. Took " << std::chrono::duration_cast<std::chrono::seconds>(EndTime - StartTime).count()
        << " seconds.\n\n";
}

inline void printInitialResults(std::ostream &Out, size_t TotalTested, int Reachable)
{
    float P = TotalTested > 0 ? (Reachable * 100.0f / TotalTested) : 0.0f;
    Out << std::string(100, '=') << "\n\nSummary:\n  Total mirrors tested: " << TotalTested
        << "\n  Reachable mirrors: " << Reachable << " (" << P
        << "%)\n  Unreachable mirrors: " << (TotalTested - Reachable) << " (" << (100.0f - P) << "%)\n";
}

template <typename T, typename Comparator, typename MetricT>
inline void printTopResults(std::ostream &Out, const std::vector<T> &Source, int TopCount, Comparator Comp,
                            std::string_view HeadingText, MetricT T::*MetricMember, std::string_view MetricUnit)
{
    auto Temp = Source;
    std::sort(Temp.begin(), Temp.end(), Comp);
    Out << "\n  Top " << TopCount << " " << HeadingText << ":\n" << std::fixed << std::setprecision(2);
    for (size_t I = 0; I < static_cast<size_t>(TopCount) && I < Temp.size(); ++I)
        Out << "    " << (I + 1) << ". " << dm_url(Temp[I].Mirror) << " - " << (Temp[I].*MetricMember) << MetricUnit
            << "\n";
}

std::vector<DebianMirror> filterMirrors(const std::vector<DebianMirror> &M, bool Excl, bool Only, const std::string &L)
{
    std::vector<DebianMirror> F;
    F.reserve(M.size());
    auto TC = dmt::countryCodeToName(L);
    for (const auto &Mi : M)
    {
        auto IsOff = dm_url(Mi).find("debian.org") != std::string::npos;
        if ((L.empty() || dm_country(Mi) == TC) && ((Only && IsOff) || (Excl && !IsOff) || (!Excl && !Only)))
            F.push_back(Mi);
    }
    return F;
}

int main(int argc, char *argv[])
{
    std::string Location;
    bool CountrySpecified = false, ExcludeOfficial = false, OnlyOfficial = false;
    long TopCount = 5;
    if (argc > 4096)
    {
        std::cerr << "Error: too many arguments\n";
        return 1;
    }

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
            TopCount = myStoi("--count", argv[++i]);
        }
        else if (Arg == "--help")
        {
            std::cout << howdy();
            return 0;
        }
        else if (Arg == "--timeout" && i + 1 < argc)
        {
            long TA = myStoi("--timeout", argv[++i]);
            PerformanceTester::RequestTimeoutMs.store(TA);
            if (TA < 1000L)
            {
                std::cerr << "\nWARNING: a timeout of less than 1 second is not recommended.\nAs a reminder, --timeout "
                             "takes a time value measured in milliseconds.\nDid you definitely mean to run the program "
                             "with such a short timeout? (y/n): ";
                char R;
                std::cin >> R;
                if (R != 'y' && R != 'Y')
                    std::exit(EXIT_FAILURE);
                std::cerr << "\n";
            }
            else if (TA < 3000L)
            {
                std::cout
                    << "Note: A timeout of less than 3 seconds may lead to many mirrors being marked as unreachable.\n";
            }
            else if (TA == kDisabledFlag)
            {
                std::cout << "Request timeouts are disabled.\n";
            }
            else
            {
                std::cout << "Using request timeout of " << (TA / 1000.0f) << " seconds (" << TA << " ms)\n";
            }
        }
        else
        {
            std::cerr << "Error: Unknown option '" << Arg << "'\n";
            return 1;
        }
    }

    if (CountrySpecified && Location.empty())
    {
        std::cerr << "Error: --country requires a valid ISO 3166-1 alpha-2 country code\n";
        return 1;
    }
    if (!Location.empty() && dmt::countryCodeToName(Location).empty())
    {
        std::cerr << "Error: Invalid country code '" << Location << "'\n";
        return 1;
    }

    std::vector<DebianMirror> Mirrors;
    if (OnlyOfficial)
    {
        if (!Location.empty())
        {
            auto TC = dmt::countryCodeToName(Location);
            if (TC.empty())
            {
                std::cerr << "Error: Invalid country code '" << Location << "'\n";
                return 1;
            }
            std::cout << "Using official mirrors for " << TC << "...\n";
            Mirrors = MirrorFetcher::getOfficialMirrors(TC);
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
    if (!OnlyOfficial)
    {
        Mirrors = filterMirrors(Mirrors, ExcludeOfficial, OnlyOfficial, Location);
        if (CountrySpecified || ExcludeOfficial)
            std::cout << "After filtering: " << Mirrors.size() << " mirrors.\n";
    }
    if (static_cast<size_t>(TopCount) > Mirrors.size())
    {
        std::cout << "We were hoping to display results for the top " << TopCount << " mirrors, but "
                  << "I could only find " << Mirrors.size() << " valid mirrors, so I'll be sure everything is "
                  << "displayed in the final results.\n";
        TopCount = static_cast<int>(Mirrors.size());
    }

    auto StartTime = std::chrono::steady_clock::now();
    auto Results = PerformanceTester::testAllMirrors(Mirrors);
    printTestingDuration(std::cout, StartTime, std::chrono::steady_clock::now());
    int Reachable = 0;
    double AvgDelay = 0.0, AvgSpeed = 0.0;
    for (const auto &R : Results)
        if (R.IsReachable)
        {
            Reachable++;
            AvgDelay += R.TransferDelayMs;
            AvgSpeed += R.DownloadSpeedMbps;
        }

    printInitialResults(std::cout, Results.size(), Reachable);
    if (Reachable > 0)
    {
        std::cout << "\n  Average time to start transfer: " << std::fixed << std::setprecision(2)
                  << (AvgDelay / Reachable / 1000.0f) << " seconds\n"
                  << "  Average speed: " << (AvgSpeed / Reachable) << " Mbps\n";
        std::vector<PerformanceResult> RR;
        for (const auto &R : Results)
            if (R.IsReachable)
                RR.push_back(R);
        printTopResults(std::cout, RR, TopCount, compareByLatency, "ranked by time to start transfer",
                        &PerformanceResult::TransferDelayMs, " ms");
        printTopResults(std::cout, RR, TopCount, compareBySpeed, "ranked by overall download speed",
                        &PerformanceResult::DownloadSpeedMbps, " Mbps");
    }
    return 0;
}
