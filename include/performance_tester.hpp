/*
 *
 * performance_tester.hpp
 *
 * Macros for use in performance_tester.cpp,
 * the PerformanceResult struct (which stores the results of a mirror test),
 * and the PerformanceTester class (which performs latency and download speed tests,
 * and also defines the file to be downloaded for testing).
 *
 */

#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include "structs.hpp"

#define RETURN_MEASUREMENT_FAILED return -1.0

#define START_MEASURED_CURL_PERFORMANCE                  \
    const auto Start = std::chrono::steady_clock::now(); \
    CURLcode Res = curl_easy_perform(curl);              \
    const auto End = std::chrono::steady_clock::now();

#define WRAP_UP_MEASURED_CURL_PERFORMANCE                                                     \
    const auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(End - Start); \
    curl_easy_cleanup(curl);

struct PerformanceResult
{
    DebianMirror Mirror;
    double LatencyMs;
    double DownloadSpeedMbps;
    bool IsReachable;
    std::string ErrorMessage;
};

class PerformanceTester
{
public:
    static PerformanceResult testMirror(const DebianMirror &Mirror);
    static std::vector<PerformanceResult> testAllMirrors(const std::vector<DebianMirror> &Mirrors);

    // Initialized to 10 seconds, or 10000 milliseconds
    inline static std::atomic<long> RequestTimeoutMs{10000};

private:
    // ls-lR.gz is universally available on Debian mirrors, and is a
    // 13MB file, so it seems like a reasonable option for measuring
    // download speed.
    static constexpr const char *TestFilePath = "/ls-lR.gz";

    // Returns the latency in milliseconds, or -1 if failed
    // Measures latency; on failure returns -1 and fills error_message.
    static double measureLatencyMs(const std::string &Url, std::string &ErrorMessage);

    // Returns the download speed in Mbps, or -1 if failed
    static double measureDownloadSpeedMbps(const std::string &Url);
};
