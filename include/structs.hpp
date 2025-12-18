/*
 *
 * structs.hpp
 *
 * Structs used across the project for storing mirror and country information.
 *
 */

#pragma once

#include <string>

// a struct to pair a country name with its 2 letter ISO code.
// for example, {"South Africa", "ZA"}
struct CountryCode
{
    const char *Name;
    const char *Code;
};

// maps Debian official mirrors to a country.
// Debian official mirrors are anything that has a debian.org URL.
// Since these URLs are non-changing, these are stored in an array
// as there is no need to fetch this information from the Debian website.
struct OfficialMirror
{
    const char *Country;
    const char *Url;
};

// Associates all needed information to manage a mirror.
struct DebianMirror
{
    std::string Url;
    std::string Country;
};
