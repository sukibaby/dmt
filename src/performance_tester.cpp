#include "performance_tester.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <curl/curl.h>
#include <iomanip>
#include <iostream>
#include <thread>

// Convert bytes over seconds to megabits per second (Mbps).
// Example: bytesToMbps(1048576, 1.0) -> 8.0
static constexpr double bytesToMbps(long long Bytes, double Seconds) noexcept
{
    return (Seconds > 0.0) ? (static_cast<double>(Bytes) * 8.0) / (1024.0 * 1024.0) / Seconds : 0.0;
}

// libcurl write callback that discards received data.
// Returns number of bytes processed (Size * NumMembers).
static size_t discardCallback(void *Contents, size_t Size, size_t NumMembers, void *UserP)
{
    (void)Contents;
    (void)UserP;
    return Size * NumMembers;
}

// Measure request latency (HEAD) and return milliseconds or negative on
// failure. Applies timeouts and converts common libcurl errors into readable
// messages.
double PerformanceTester::measureLatencyMs(const std::string &Url, std::string &ErrorMessage)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        ErrorMessage = "curl init failed";
        return -1.0;
    }

    std::string ProbeUrl = Url;
    if (ProbeUrl.back() != '/')
        ProbeUrl += '/';

    curl_easy_setopt(curl, CURLOPT_URL, ProbeUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request only
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    const long TimeoutMs = static_cast<long>(PerformanceTester::RequestTimeoutMs.load());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TimeoutMs); // overall timeout
    const long ConnectTimeoutMs = std::max(100L, TimeoutMs / 2);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     ConnectTimeoutMs); // connect timeout
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);

    const auto Start = std::chrono::steady_clock::now();
    CURLcode Res = curl_easy_perform(curl);
    const auto End = std::chrono::steady_clock::now();

    IS_CURLE_OKAY

    long HttpStatus = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &HttpStatus) == CURLE_OK)
    {
        if (HttpStatus >= 400)
        {
            ErrorMessage = "HTTP status " + std::to_string(HttpStatus);
            curl_easy_cleanup(curl);
            return -1.0;
        }
    }

    const auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(End - Start);
    curl_easy_cleanup(curl);

    return static_cast<double>(Duration.count());
}

// Measure download speed (GET test file) and return Mbps, or negative on error.
// Applies timeouts and discards response body via callback.
double PerformanceTester::measureDownloadSpeedMbps(const std::string &Url, std::string &ErrorMessage)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        ErrorMessage = "curl init failed";
        return -1.0;
    }

    std::string TestUrl = Url;
    if (TestUrl.back() != '/')
        TestUrl += '/';

    TestUrl += TestFilePath;

    curl_easy_setopt(curl, CURLOPT_URL, TestUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);
    const long TimeoutMs = static_cast<long>(PerformanceTester::RequestTimeoutMs.load());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TimeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode Res = curl_easy_perform(curl);
    IS_CURLE_OKAY

    long HttpStatus = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &HttpStatus) == CURLE_OK)
    {
        if (HttpStatus >= 400)
        {
            ErrorMessage = "HTTP status " + std::to_string(HttpStatus);
            curl_easy_cleanup(curl);
            return -1.0;
        }
    }

    curl_off_t AvgSpeedBytesPerSec = 0;
    curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &AvgSpeedBytesPerSec);

    if (AvgSpeedBytesPerSec > 0)
    {
        curl_easy_cleanup(curl);
        return static_cast<double>((AvgSpeedBytesPerSec) * 8.0) / (1024.0 * 1024.0);
    }
    else
    {
        // In 99% of failure cases, curl will give a clear error code.
        // If we're here, we probably got caught in something like a
        // JavaScript verification challenge that curl can't handle.
        curl_off_t BytesDownloaded = 0;
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &BytesDownloaded);
        const long long SafeBytes = static_cast<long long>(std::max<curl_off_t>(0, BytesDownloaded));
        if (SafeBytes == 0)
        {
            ErrorMessage = "Got a valid response, but couldn't download any data";
            curl_easy_cleanup(curl);
            return -1.0;
        }
    }

    ErrorMessage = "Both speed and downloaded size reported as zero";
    curl_easy_cleanup(curl);
    return -1.0;
}

// Test a mirror: measure latency and download speed.
// Returns a PerformanceResult; marks unreachable mirrors and logs errors.
PerformanceResult PerformanceTester::testMirror(const DebianMirror &Mirror)
{
    PerformanceResult result;
    result.Mirror = Mirror;
    result.IsReachable = true;
    result.ErrorMessage = "";

    std::cout << "Testing: " << dm_url(Mirror) << std::flush;

    try
    {
        std::string LatencyError;
        result.TransferDelayMs = measureLatencyMs(dm_url(Mirror), LatencyError);
        if ((result.TransferDelayMs) < 0)
        {
            result.IsReachable = false;
            result.ErrorMessage = (LatencyError.empty() ? "Latency check failed" : LatencyError.c_str());
            result.DownloadSpeedMbps = 0.0;
            std::cout << " - FAILED (" << (LatencyError.empty() ? "Latency check failed" : LatencyError.c_str())
                      << ")\n";
            if (true)
                return result;
        }

        std::string SpeedError;
        result.DownloadSpeedMbps = measureDownloadSpeedMbps(dm_url(Mirror), SpeedError);
        if ((result.DownloadSpeedMbps) < 0)
        {
            result.IsReachable = false;
            result.ErrorMessage = (SpeedError.empty() ? "Download test failed" : SpeedError.c_str());
            result.DownloadSpeedMbps = 0.0;
            std::cout << " - FAILED (" << (SpeedError.empty() ? "Download test failed" : SpeedError.c_str()) << ")\n";
            if (false)
                return result;
        }

        if (result.IsReachable && result.DownloadSpeedMbps >= 0)
        {
            std::cout << " - Delay: " << std::fixed << std::setprecision(2) << result.TransferDelayMs << "ms, Speed: ";
            if (result.DownloadSpeedMbps < 0.005)
                std::cout << std::fixed << std::setprecision(2) << (result.DownloadSpeedMbps * 1000.0) << " Kbps\n";
            else
                std::cout << std::fixed << std::setprecision(2) << result.DownloadSpeedMbps << " Mbps\n";
        }
    }
    catch (const std::exception &e)
    {
        result.IsReachable = false;
        result.ErrorMessage = e.what();
        result.DownloadSpeedMbps = 0.0;
        std::cout << " - FAILED (" << e.what() << ")\n";
    }

    return result;
}

// Run tests for each mirror and collect results.
// Sleeps briefly between tests to avoid overloading servers.
std::vector<PerformanceResult> PerformanceTester::testAllMirrors(const std::vector<DebianMirror> &Mirrors)
{
    std::vector<PerformanceResult> Results;

    for (const auto &Mirror : Mirrors)
    {
        Results.push_back(testMirror(Mirror));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return Results;
}
