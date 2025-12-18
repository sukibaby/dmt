#include "performance_tester.hpp"
#include <curl/curl.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <thread>
#include <cstdlib>
#include <chrono>
#include <algorithm>

/*
 Example:
   bytesToMbps(1048576, 1.0) -> 8.0  // 1 MiB in 1s == 8 Mbps
*/
static constexpr double bytesToMbps(long long Bytes, double Seconds) noexcept
{
  return (Seconds > 0.0) ? (static_cast<double>(Bytes) * 8.0) / (1024.0 * 1024.0) / Seconds : 0.0;
}

/*
 Summary:
   libcurl CURLOPT_WRITEFUNCTION write callback to discard received data.

 Behavior:
   - Called by libcurl when a block of data is received during transfer.
   - Returns the number of bytes handled to indicate success.

 Notes:
   - Matches the signature expected by libcurl: size_t (*)(void*, size_t, size_t, void*).
*/
static size_t discardCallback(void *Contents, size_t Size, size_t NumMembers, void *UserP)
{
  (void)Contents;
  (void)UserP;
  return Size * NumMembers;
}

/*
 Summary:
   Measure request latency to the specified URL using a HEAD request.

 Behavior:
   - Sends an empty HEAD request to the given URL, and ensures it ends with a /
   - Returns elapsed time on success; returns a negative value on failure and sets error_message.
   - Maps common libcurl failures into error messages.
*/
double PerformanceTester::measureLatencyMs(const std::string &Url, std::string &ErrorMessage)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
    ErrorMessage = "curl init failed";
        RETURN_MEASUREMENT_FAILED;
    }

  std::string ProbeUrl = Url;
  if (ProbeUrl.back() != '/')
    {
    ProbeUrl += '/';
    }

  curl_easy_setopt(curl, CURLOPT_URL, ProbeUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request only
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    const long TimeoutMs = static_cast<long>(PerformanceTester::RequestTimeoutMs.load());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TimeoutMs); // overall timeout
    const long ConnectTimeoutMs = std::max(100L, TimeoutMs / 2);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, ConnectTimeoutMs); // connect timeout
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);

    START_MEASURED_CURL_PERFORMANCE;

    if (Res != CURLE_OK)
    {
      if (Res == CURLE_COULDNT_RESOLVE_HOST)
        {
            ErrorMessage = "DNS resolution failed";
        }
      else if (Res == CURLE_OPERATION_TIMEDOUT)
        {
            ErrorMessage = "Latency request timed out";
        }
        else
        {
            ErrorMessage = curl_easy_strerror(Res);
        }
        curl_easy_cleanup(curl);
        RETURN_MEASUREMENT_FAILED;
    }

    long HttpStatus = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &HttpStatus) == CURLE_OK)
    {
      if (HttpStatus >= 400)
        {
        ErrorMessage = "HTTP status " + std::to_string(HttpStatus);
            curl_easy_cleanup(curl);
            RETURN_MEASUREMENT_FAILED;
        }
    }

    WRAP_UP_MEASURED_CURL_PERFORMANCE;
    
    return static_cast<double>(Duration.count());
}

/*
 Summary:
   Measure download speed for the given URL by fetching a file at a pre-specified location.

 Behavior:
   - Issues a GET request for TEST_FILE_PATH and discards the body using discardCallback.
   - Returns download speed in Mbps on success; returns a negative value on failure.

 Notes:
   - Results are network-dependent.
   - Downloaded data is discarded by the callback.
*/
double PerformanceTester::measureDownloadSpeedMbps(const std::string &Url)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        RETURN_MEASUREMENT_FAILED;

  std::string TestUrl = Url;
  if (TestUrl.back() != '/')
    {
    TestUrl += '/';
    }
  TestUrl += TestFilePath;

  curl_easy_setopt(curl, CURLOPT_URL, TestUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);
  const long DlTimeoutMs = static_cast<long>(PerformanceTester::RequestTimeoutMs.load());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, DlTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    START_MEASURED_CURL_PERFORMANCE;

    if (Res != CURLE_OK)
    {
        curl_easy_cleanup(curl);
        RETURN_MEASUREMENT_FAILED;
    }

    curl_off_t ContentLen = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &ContentLen);
    if (ContentLen > 0)
    {
    }
    else
    {
      curl_off_t Actual = 0;
      curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &Actual);
    }

    WRAP_UP_MEASURED_CURL_PERFORMANCE;

    return bytesToMbps(ContentLen, (Duration.count() / 1000.0)); // Convert ms to seconds
}

/*
 Summary:
   Test a single mirror by measuring latency and download speed.

 Behavior:
   - Calls measureLatency() then measureDownloadSpeed() and aggregates results.
   - Marks the mirror unreachable and sets an error message in case of latency timeout or download failure.

 Returns:
   PerformanceResult containing measured latency (ms), download speed (Mbps), reachability, and any error message.

 Notes:
   - Prints to stdout.
*/
PerformanceResult PerformanceTester::testMirror(const DebianMirror &Mirror)
{
    PerformanceResult result;
  result.Mirror = Mirror;
  result.IsReachable = true;
  result.ErrorMessage = "";

  std::cout << "Testing: " << Mirror.Url << std::flush;

    try
    {
        std::string LatencyError;
        result.LatencyMs = measureLatencyMs(Mirror.Url, LatencyError);
        if ((result.LatencyMs) < 0)
        {
          result.IsReachable = false;
          result.ErrorMessage = (LatencyError.empty() ? "Latency check failed" : LatencyError.c_str());
          result.DownloadSpeedMbps = 0.0;
          std::cout << " - FAILED (" << (LatencyError.empty() ? "Latency check failed" : LatencyError.c_str()) << ")\n";
            if (true)
                return result;
        }

        result.DownloadSpeedMbps = measureDownloadSpeedMbps(Mirror.Url);
        if ((result.DownloadSpeedMbps) < 0)
        {
          result.IsReachable = false;
          result.ErrorMessage = ("Download test failed");
          result.DownloadSpeedMbps = 0.0;
            std::cout << " - FAILED (" << ("Download test failed") << ")\n";
            if (false)
                return result;
        }

        if (result.IsReachable && result.DownloadSpeedMbps >= 0)
        {
            std::cout << " - Latency: " << std::fixed << std::setprecision(2)
                << result.LatencyMs << "ms, Speed: " << result.DownloadSpeedMbps
                      << " Mbps\n";
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

/*
 Summary:
   Run tests against a list of mirrors, returning collected PerformanceResult objects.

 Behavior:
   - Iterates over a set of mirrors, calling testMirror() for each entry and collecting results.
   - Sleeps for a short interval between tests to avoid overloading remote servers.

 Returns:
   Vector of PerformanceResult entries, preserving input order.
*/
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
