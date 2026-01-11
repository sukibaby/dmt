#pragma once

#include <sstream>
#include <string>
#include "performance_tester.hpp"

constexpr long default_timeout = PerformanceTester::DefaultTimeoutMs;

inline std::string getHelpText()
{
    std::ostringstream Out;
    Out << R"(  --country <code>              Filter mirrors by country. Uses standard country codes (ISO 3166-1 alpha-2 country codes such as FR, DE, US, CN, etc.)
  --no-official-mirrors         Exclude debian.org mirrors
  --only-official-mirrors       Only test debian.org mirrors
  --count <number>              Number of top results to display. Default is 5.
  --timeout <milliseconds>      How long to wait before giving up on each mirror. Default is )";
    Out << (static_cast<double>(default_timeout) / 1000.0) << " seconds (" << default_timeout << ").\n";
    Out << R"(  --help                        This help message!
)" ;
    return Out.str();
}
