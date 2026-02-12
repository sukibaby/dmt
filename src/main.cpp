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

namespace {
bool compareByLatency(const PerformanceResult &A, const PerformanceResult &B) {
    return A.transferDelayMs < B.transferDelayMs;
}

bool compareBySpeed(const PerformanceResult &A, const PerformanceResult &B) {
    return A.downloadSpeedMbps > B.downloadSpeedMbps;
}

long safeStringToLong(const char *flag, const char *inputStr) {
    long long parsedValue = -1;

    if (strcmp(inputStr, "none") == 0) {
        return OPTION_DISABLED;
    }

    auto [endPointer, errorCode] =
        std::from_chars(inputStr, inputStr + std::strlen(inputStr), parsedValue);

    if (parsedValue == -1) {
        std::cerr << "Error when assigning value for " << flag << "\n";
        std::exit(EXIT_FAILURE);
    }

    // Assuming the user intentionally meant "as large as possible" as a way to
    // disable timeouts / ranking cutoffs. We need to check out_of_range first
    // for consistency between compilers.
    if (errorCode == std::errc::result_out_of_range ||
        parsedValue >= (long long)std::numeric_limits<long>::max()) {
        return OPTION_DISABLED;
    }

    if (errorCode == std::errc::invalid_argument || parsedValue == 0) {
        std::cerr << "Error: " << flag << " value is out of range.\n";
        std::exit(EXIT_FAILURE);
    }

    if (parsedValue < 0) {
        std::cerr << "Error: " << flag << " value cannot be negative.\n";
        std::exit(EXIT_FAILURE);
    }

    return static_cast<long>(parsedValue);
}

const std::vector<std::pair<const char *, const char *>> helpOptions = {
    {"--country <code>", "Filter mirrors by country. Uses ISO 3166-1 alpha-2 "
                         "country codes (such as FR, DE, US, CN, etc.)"},
    {"--no-official-mirrors", "Exclude debian.org mirrors"},
    {"--only-official-mirrors", "Only test debian.org mirrors"},
    {"--count <number>",
     "Number of top results to display. By default, the top 5 are shown."},
    {"--timeout <milliseconds>",
     "How long to wait before giving up, per mirror."},
    {"--help", "This help message! For further details, please see the man "
               "page (man dmt)."},
};

void printHelp() {
    for (const auto &opt : helpOptions) {
        std::cout << "  " << std::left << std::setw(32) << opt.first
                  << opt.second << "\n";
    }
    std::exit(EXIT_SUCCESS);
}
} // namespace

// Helper for printing the sorted results after testing has been completed.
// Uses the value from --count, if provided, to determine how many results to display.
// Does not modify the source vector.
template <typename T, typename Comparator, typename MetricT>
void printTopResults(std::ostream &outputStream, const std::vector<T> &source,
                     int topCount, Comparator comp, MetricT T::*metricMember,
                     std::string_view metricUnit) {
    std::vector<T> Temp = source;
    std::sort(Temp.begin(), Temp.end(), comp);

    for (size_t IIDX = 0;
         IIDX < static_cast<size_t>(topCount) && IIDX < Temp.size(); ++IIDX) {
        const auto &Item = Temp[IIDX];
        outputStream << "    " << (IIDX + 1) << ". " << dm_url(Item.Mirror) << " - "
            << std::fixed << std::setprecision(2) << (Item.*metricMember)
            << metricUnit << "\n";
    }
}

// Filter Debian mirrors by country and official status.
// Returns a vector of mirrors matching the given filters.
std::vector<DebianMirror>
filterMirrors(const std::vector<DebianMirror> &mirrors, bool excludeOfficials,
              bool onlyOfficials, const std::string &mirrorLocation) {
    std::vector<DebianMirror> filtered;
    filtered.reserve(mirrors.size());
    std::string targetCountry = CountryCodes::getName(mirrorLocation);

    for (const auto &Mirror : mirrors) {
        bool isOfficial =
            dm_url(Mirror).find("debian.org") != std::string::npos;
        bool matchesLocation =
            mirrorLocation.empty() || dm_country(Mirror) == targetCountry;

        if (!matchesLocation)
            continue;

        if (onlyOfficials && isOfficial)
            filtered.push_back(Mirror);
        else if (excludeOfficials && !isOfficial)
            filtered.push_back(Mirror);
        else if (!excludeOfficials && !onlyOfficials)
            filtered.push_back(Mirror);
    }

    return filtered;
}

// Main entry point: parse args, fetch mirrors, run tests.
// Returns 0 on success, non-zero on failure.
int main(int argc, char *argv[]) {
    // Setup default arguments
    std::string location;
    bool countrySpecified = false;
    bool excludeOfficials = false;
    bool officialsOnly = false;
    bool loneStandingServer = false;
    long topCount = 5L; // default number of "Top" results to display

    // Check argument size before parsing
    if (argc < 0)
        return 1; // defensive
    if (argc > 4096) {
        std::cerr << "Error: too many arguments\n";
        return 1;
    }

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string cmdLineArg = argv[i];
        if (cmdLineArg == "--country" && i + 1 < argc) {
            countrySpecified = true;
            location = CountryCodes::normalize(argv[++i]);
        } else if (cmdLineArg == "--no-official-mirrors") {
            excludeOfficials = true;
        } else if (cmdLineArg == "--only-official-mirrors") {
            officialsOnly = true;
        } else if (cmdLineArg == "--count" && i + 1 < argc) {
            // We'll clamp this later after fetching mirrors. So for now, just
            // parse/validate.
            const char *countValueString = argv[++i];
            topCount = safeStringToLong("--count", countValueString);
        } else if (cmdLineArg == "--help") {
            printHelp();
        } else if (cmdLineArg == "--timeout" && i + 1 < argc) {
            const char *timeoutValueString = argv[++i];
            long timeoutArgs = safeStringToLong("--timeout", timeoutValueString);

            if (timeoutArgs == OPTION_DISABLED) {
                std::cout << "Request timeouts are disabled.\n";
                PerformanceTester::RequestTimeoutMs.store(OPTION_DISABLED);
                continue;
            } else if (timeoutArgs < 1000L) {
                std::cerr << "\nWARNING: a timeout of less than 1 second is "
                             "not recommended.\n"
                          << "As a reminder, --timeout takes a time value "
                             "measured in milliseconds.\n"
                          << "Did you definitely mean to run the program with "
                             "such a short timeout? (y/n): ";
                char userResponse;
                std::cin >> userResponse;
                if (userResponse != 'y' && userResponse != 'Y')
                    std::exit(EXIT_FAILURE);
                std::cerr << "\n";
            } else if (timeoutArgs < 3000L) {
                std::cout << "Note: A timeout of less than 3 seconds may lead "
                             "to many mirrors being marked as unreachable.\n";
            } else {
                const float timeoutSeconds =
                    static_cast<float>(timeoutArgs) / 1000.0f;
                std::cout << "Using timeout value of " << timeoutSeconds
                          << " seconds (" << timeoutArgs << " ms)\n";
            }
            PerformanceTester::RequestTimeoutMs.store(
                static_cast<long>(timeoutArgs));
        } else {
            std::cerr << "Error: Unknown option '" << cmdLineArg << "'\n";
            return 1;
        }
    }

    // Validate that --country was given a valid code
    if (countrySpecified && location.empty()) {
        std::cerr << "Error: --country requires a valid ISO 3166-1 alpha-2 "
                     "country code\n";
        return 1;
    }

    // Validate country code
    if (!location.empty()) {
        std::string invalidCountry = CountryCodes::getName(location);
        if (invalidCountry.empty()) {
            std::cerr << "Error: Invalid country code '" << location << "'\n";
            return 1;
        }
    }

    // If only-official-mirrors is specified, use predefined official mirrors to
    // avoid a web fetch
    std::vector<DebianMirror> debianMirrorList;
    if (officialsOnly) {
        if (!location.empty()) {
            std::string officialsCountry = CountryCodes::getName(location);
            if (!officialsCountry.empty()) {
                std::cout << "Using official mirrors for " << officialsCountry
                          << "...\n";
                debianMirrorList = MirrorFetcher::getOfficialMirrors(officialsCountry);
            }
        } else {
            std::cout << "Using all official Debian mirrors...\n";
            debianMirrorList = MirrorFetcher::getOfficialMirrors();
        }
    } else {
        std::cout << " - Fetching mirror list...\n";
        debianMirrorList = MirrorFetcher::fetchMirrors();
    }

    if (debianMirrorList.empty()) {
        std::cerr << "Error: Failed to fetch mirrors\n";
        return 1;
    }

    if (debianMirrorList.size() == 1) {
        std::cout << "It looks like there's only a single mirror available.\n";
        loneStandingServer = true;
    } else {
        std::cout << " - Fetched " << debianMirrorList.size() << " mirrors.\n";
    }

    // Apply filtering only if we fetched from the web (not using predefined
    // official mirrors)
    if (!officialsOnly) {
        debianMirrorList =
            filterMirrors(debianMirrorList, excludeOfficials, officialsOnly, location);
        if (countrySpecified || excludeOfficials)
            std::cout << " - After filtering: " << debianMirrorList.size() << " mirrors.\n";
    }

    // Now that we know how many mirrors we have, ensure topCount is not some
    // larger value. If we got the disabled flag, then internally that really
    // just means to set this value to match whatever the number of mirrors we
    // tested in total.
    if (topCount == OPTION_DISABLED ||
        static_cast<size_t>(topCount) > debianMirrorList.size())
        topCount = static_cast<long>(debianMirrorList.size());

    const auto timerStartPoint = std::chrono::steady_clock::now();
    auto mirrorResults = PerformanceTester::testAllMirrors(debianMirrorList);
    const auto timerEndPoint = std::chrono::steady_clock::now();
    const auto timerDuration =
        std::chrono::duration_cast<std::chrono::seconds>(timerEndPoint - timerStartPoint);
    std::cout << "Testing complete. Took " << timerDuration.count()
              << " seconds.\n";

    // If we only had a single server, there is no need to continue past this point.
    if (loneStandingServer) {
        return 0;
    }

    int reachableCount = 0;
    double averageNetworkDelay = 0.0;
    double averageSpeed = 0.0;

    for (const auto &Result : mirrorResults) {
        if (Result.IsReachable) {
            reachableCount++;
            averageNetworkDelay += Result.transferDelayMs;
            averageSpeed += Result.downloadSpeedMbps;
        }
    }

    const float percentReachable =
        (mirrorResults.size() > 0) ? (static_cast<float>(reachableCount) /
                                static_cast<float>(mirrorResults.size()) * 100.0f)
                             : 0.0f;

    std::cout << "\n" << std::string(100, '=') << "\n\n";
    std::cout << "Summary:\n";
    std::cout << "  Total mirrors tested: " << mirrorResults.size() << "\n";
    std::cout << "  Reachable mirrors: " << reachableCount << " ("
              << percentReachable << "%)\n";
    std::cout << "  Unreachable mirrors: "
              << (mirrorResults.size() - static_cast<size_t>(reachableCount)) << " ("
              << (100.0f - percentReachable) << "%)\n";

    if (reachableCount > 0) {
        const float avgDelayInSeconds =
            (static_cast<float>(averageNetworkDelay) /
             static_cast<float>(reachableCount)) /
            1000.0f;
        std::cout << "\n  Average time to start transfer: " << std::fixed
                  << std::setprecision(2) << avgDelayInSeconds << " seconds\n";
        std::cout << "  Average speed: " << std::fixed << std::setprecision(2)
                  << (averageSpeed / reachableCount) << " Mbps\n";

        std::vector<PerformanceResult> reachableResults;
        for (const auto &mirrorTestResult : mirrorResults) {
            if (mirrorTestResult.IsReachable)
                reachableResults.push_back(mirrorTestResult);
        }

        const size_t displayCount =
            std::min(static_cast<size_t>(topCount), reachableResults.size());

        std::cout << "\n  Top " << displayCount
                  << " ranked by time to start transfer:\n";
        printTopResults(std::cout, reachableResults,
                        static_cast<int>(displayCount), compareByLatency,
                        &PerformanceResult::transferDelayMs, " ms");

        std::cout << "\n  Top " << displayCount
                  << " ranked by overall download speed:\n";
        printTopResults(std::cout, reachableResults,
                        static_cast<int>(displayCount), compareBySpeed,
                        &PerformanceResult::downloadSpeedMbps, " Mbps");
    }

    std::cout << "\n";

    return 0;
}
