#pragma once

#include <string>

#ifndef _WIN32
#include <sys/types.h>
#endif

enum class platform_type { linux, darwin, windows };

enum class line_ending { raw, lf, crlf };

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
std::string file_hash(const std::string &path, line_ending le = line_ending::raw);
bool file_hash_normalize(const std::string &path, const std::string &expected);

// POSIX-only: permission bits (st_mode & 0777). Returns -1 on error or on
// platforms where modes don't apply (Windows).
int file_mode(const std::string &path);

// POSIX-only: the gid that `chown_user` transfers files to. Prefers the
// "users" group when it exists, else gid 1. Only defined on POSIX (gid_t).
#ifndef _WIN32
gid_t install_group();
#endif

// POSIX-only: true when the file is owned by the invoking user with the
// chown_user group (see install_group()). Always true on Windows (ownership
// transfer is a no-op there).
bool file_matches_ownership(const std::string &path);

}
