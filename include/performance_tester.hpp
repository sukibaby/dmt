#pragma once

#include "structs.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#define START_MEASURED_CURL_PERFORMANCE                                                                                \
    const auto Start = std::chrono::steady_clock::now();                                                               \
    CURLcode Res = curl_easy_perform(curl);                                                                            \
    const auto End = std::chrono::steady_clock::now();

#define WRAP_UP_MEASURED_CURL_PERFORMANCE                                                                              \
    const auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(End - Start);                          \
    curl_easy_cleanup(curl);

#define IS_CURLE_OKAY                                                                                                  \
    if (Res != CURLE_OK)                                                                                               \
    {                                                                                                                  \
        if (Res == CURLE_COULDNT_RESOLVE_HOST)                                                                         \
            ErrorMessage = "DNS resolution failed";                                                                    \
        else if (Res == CURLE_OPERATION_TIMEDOUT)                                                                      \
            ErrorMessage = "Time limit of " + std::to_string(TimeoutMs) + " ms reached - moving on";                   \
        else                                                                                                           \
            ErrorMessage = curl_easy_strerror(Res);                                                                    \
        curl_easy_cleanup(curl);                                                                                       \
        return -1.0;                                                                                                   \
    }

struct PerformanceResult
{
    DebianMirror Mirror;
    double TransferDelayMs;
    double DownloadSpeedMbps;
    bool IsReachable;
    std::string ErrorMessage;
};

class PerformanceTester
{
  public:
    // Default timeout set to 1 minute, represented as milliseconds.
    // Most requests won't take anywhere near this long, but I don't
    // want to cause avoidable failures on slow or distant mirrors,
    // or for users with slow / unreliable connections. People can
    // always set a different value with the --timeout option.
    static constexpr long DefaultTimeoutMs = 60000;

    static PerformanceResult testMirror(const DebianMirror &Mirror);
    static std::vector<PerformanceResult> testAllMirrors(const std::vector<DebianMirror> &Mirrors);

    inline static std::atomic<long> RequestTimeoutMs{DefaultTimeoutMs};

  private:
    // ls-lR.gz is universally available on Debian mirrors, and is a
    // 13MB file, so it seems like a reasonable option for measuring
    // download speed.
    static constexpr const char *TestFilePath = "/ls-lR.gz";

    // Returns the latency in milliseconds, or -1 if failed
    // Measures latency; on failure returns -1 and fills error_message.
    static double measureLatencyMs(const std::string &Url, std::string &ErrorMessage);

    // Returns the download speed in Mbps, or -1 if failed
    // Measures download speed; on failure returns -1 and fills ErrorMessage.
    static double measureDownloadSpeedMbps(const std::string &Url, std::string &ErrorMessage);
};
