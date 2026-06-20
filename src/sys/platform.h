#pragma once

#include <string>

enum class platform_type { linux, darwin, windows };

namespace platform {

platform_type current_platform();
platform_type from_string(const std::string &);
std::string to_string(platform_type);

std::string which(const std::string &cmd);
std::string os();
std::string arch();
std::string exe_name(const std::string &base);
std::string exe_path();
std::string cache_dir();
bool file_exists(const std::string &path);
bool dir_exists(const std::string &path);
std::string file_hash(const std::string &path);

}
