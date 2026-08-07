#pragma once

#include <cctype>
#include <filesystem>
#include <string>

inline const std::string PREDEP_VERSION = "v0.0.2";

// ---- Safety levels for prompting ----

enum class safety_level { safe, warning, dangerous, critical };

// ---- Path resolution privilege ----
// install: fetch downloads may write to root:// or cache://
// build:   docker/package may only write to root://

enum class privilege { install, build };

namespace path {

inline const std::string root      = "root://";
inline const std::string cache     = "cache://";
inline const std::string libname   = "predep";
inline const std::string manifest  = ".predep-manifest";

enum class check_result { ok, config_error, requires_privileged };

// Check a resolved path against allowed prefixes for the given privilege.
inline check_result check(const std::string &resolved,
                          const std::string &project_root,
                          const std::string &cache_dir,
                          privilege priv)
{
    namespace fs = std::filesystem;
    auto r = fs::path(resolved).lexically_normal().string();
    auto pr = fs::path(project_root).lexically_normal().string();
    auto cd = fs::path(cache_dir).lexically_normal().string();

#ifdef _WIN32
    auto ci_starts_with = [](const std::string &str, const std::string &prefix) {
        if (prefix.empty()) return true;
        if (str.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(str[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        return true;
    };
    if (ci_starts_with(r, pr))
        return check_result::ok;
    if (ci_starts_with(r, cd))
        return priv == privilege::install
            ? check_result::ok
            : check_result::config_error;
#else
    if (r.rfind(pr, 0) == 0)
        return check_result::ok;
    if (r.rfind(cd, 0) == 0)
        return priv == privilege::install
            ? check_result::ok
            : check_result::config_error;
#endif
    return check_result::requires_privileged;
}

} // namespace path
