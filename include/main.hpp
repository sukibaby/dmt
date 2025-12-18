/*
 *
 * main.hpp
 *
 * Help text to be displayed with the --help flag.
 *
 */

#pragma once

inline constexpr const char HelpText[] = R"(  --country <code>              Filter mirrors by country. Uses standard country codes (ISO 3166-1 alpha-2 country codes such as FR, DE, US, CN, etc.)
  --no-official-mirrors         Exclude debian.org mirrors
  --only-official-mirrors       Only test debian.org mirrors
  --count <number>              Number of top results to display. Default is 5.
  --timeout <milliseconds>      How long to wait before giving up on each mirror. Default is 10 seconds (10000).
  --help                        This help message!
)";
