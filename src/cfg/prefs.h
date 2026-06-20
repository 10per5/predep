#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prefs {

struct trusted_entry
{
    std::string sha256;
    std::vector<std::string> paths;
    std::int64_t trusted_at = 0;   // unix timestamp
    bool permanent = false;
};

bool is_trusted(const std::string &sha256);
void add_trusted(const std::string &sha256, const std::string &path = "");
int  sanitize();   // returns number of entries removed
std::string prefs_path();
int  trust_time_minutes();

} // namespace prefs
