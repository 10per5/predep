#pragma once

#include <string>

namespace platform {

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
