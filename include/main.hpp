#pragma once

inline constexpr char HelpText[] =
    R"(  --country <code>              Filter mirrors by country. Uses standard country codes (ISO 3166-1 alpha-2 country codes such as FR, DE, US, CN, etc.)
  --no-official-mirrors         Exclude debian.org mirrors
  --only-official-mirrors       Only test debian.org mirrors
  --count <number>              Number of top results to display. Default is 5.
  --timeout <milliseconds>      How long to wait before giving up on each mirror. Default is 15 seconds (15000).
  --help                        This help message!

For more detailed information, check the man page (man dmt).
)";
