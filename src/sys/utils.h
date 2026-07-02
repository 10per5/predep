#pragma once

#include <string>

namespace util {

// Parse a boolean value from a string or numeric representation.
// Accepts: "true", "false", "1", "0" (case-insensitive for true/false).
inline bool to_bool(const std::string &s, bool def = false)
{
    if (s == "true" || s == "1")
        return true;
    if (s == "false" || s == "0")
        return false;
    return def;
}

} // namespace util
