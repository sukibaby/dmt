#pragma once

#include <sstream>
#include <string>
#include "performance_tester.hpp"

inline std::string getHelpText()
{
    std::ostringstream Out;
    Out << R"(  --country <code>              Filter mirrors by country. Uses ISO 3166-1 alpha-2 country codes (such as FR, DE, US, CN, etc.)
  --no-official-mirrors         Exclude debian.org mirrors
  --only-official-mirrors       Only test debian.org mirrors
  --count <number>              Number of top results to display. Default is 5.
  --timeout <milliseconds>      How long to wait before giving up on each mirror. Default is )";
    Out << (static_cast<double>(PerformanceTester::DefaultTimeoutMs) / 1000.0) << " seconds (" << PerformanceTester::DefaultTimeoutMs << ").\n";
    Out << R"(  --help                        This help message!
)" ;
    return Out.str();
}
