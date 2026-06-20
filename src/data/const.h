#pragma once

#include <string>

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
    if (resolved.rfind(project_root, 0) == 0)
        return check_result::ok;
    if (resolved.rfind(cache_dir, 0) == 0)
        return priv == privilege::install
            ? check_result::ok
            : check_result::config_error;
    return check_result::requires_privileged;
}

} // namespace path
