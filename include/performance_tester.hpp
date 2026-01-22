#pragma once

#include "structs.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

// Used in various places to represent disabled timeouts/unlimited counts.
constexpr long OPTION_DISABLED = -1L;

struct PerformanceResult {
    DebianMirror Mirror;
    double transferDelayMs;
    double downloadSpeedMbps;
    bool IsReachable;
    std::string ErrorMessage;
};

class PerformanceTester {
  public:
    // Default timeout set to 1 minute, represented as milliseconds.
    // Most requests won't take anywhere near this long, but I don't
    // want to cause avoidable failures on slow or distant mirrors,
    // or for users with slow / unreliable connections. People can
    // always set a different value with the --timeout option.
    static constexpr long DefaultTimeoutMs = 60000;

    static PerformanceResult testMirror(const DebianMirror &Mirror);
    static std::vector<PerformanceResult>
    testAllMirrors(const std::vector<DebianMirror> &Mirrors);

    // libcurl's API for timeout via CURLOPT_TIMEOUT_MS uses a long,
    // which is why this variable is also of type long.
    inline static std::atomic<long> RequestTimeoutMs{DefaultTimeoutMs};

  private:
    // ls-lR.gz is universally available on Debian mirrors, and is a
    // 13MB file, so it seems like a reasonable option for measuring
    // download speed.
    static constexpr const char *TestFilePath = "/ls-lR.gz";

    // Returns the latency in milliseconds, or -1 if failed
    // Measures latency; on failure returns -1 and fills error_message.
    static double measureLatencyMs(const std::string &Url,
                                   std::string &ErrorMessage);

    // Returns the download speed in Mbps, or -1 if failed
    // Measures download speed; on failure returns -1 and fills ErrorMessage.
    static double measureDownloadSpeedMbps(const std::string &Url,
                                           std::string &ErrorMessage);
};
