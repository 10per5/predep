#include "cfg/config.h"
#include "cfg/prefs.h"
#include "sys/platform.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace prefs {

static std::string prefs_dir()
{
    return (fs::path(platform::cache_dir()) / "preferences").string();
}

std::string prefs_path()
{
    return (fs::path(prefs_dir()) / "trusted.toml").string();
}

static std::vector<trusted_entry> &trusted_cache()
{
    static std::vector<trusted_entry> cache;
    static bool loaded = false;
    if (!loaded)
    {
        auto path = prefs_path();
        if (fs::exists(path))
        {
            try
            {
                auto root = config_node::parse_file(path);
                auto arr = root.get_array("trusted");
                for (auto &elem : arr)
                {
                    trusted_entry e;
                    e.sha256 = elem.get_string("sha256");
                    if (e.sha256.size() != 64)
                        continue;
                    bool hex = true;
                    for (auto c : e.sha256)
                        if (!std::isxdigit(static_cast<unsigned char>(c))) { hex = false; break; }
                    if (!hex)
                        continue;

                    auto path_arr = elem.get_array("paths");
                    for (auto &p : path_arr)
                        e.paths.push_back(p.as_string());

                    e.trusted_at = static_cast<std::int64_t>(elem.get_int("trusted_at", 0));
                    e.permanent = elem.get_bool_flex("permanent");
                    cache.push_back(std::move(e));
                }
            }
            catch (...)
            {
                // Malformed — start fresh
            }
        }
        loaded = true;
    }
    return cache;
}

static void flush()
{
    auto &cache = trusted_cache();
    auto path = prefs_path();
    fs::create_directories(fs::path(path).parent_path());

    std::ofstream f(path);
    f << "# Trusted predep configuration files\n";
    f << "# Manage with: predep --clear-trust or edit this file directly\n";
    f << "trust_time_minutes = " << trust_time_minutes() << "\n\n";

    for (auto &e : cache)
    {
        f << "[[trusted]]\n";
        f << "sha256 = \"" << e.sha256 << "\"\n";
        f << "trusted_at = " << e.trusted_at << "\n";
        f << "permanent = " << (e.permanent ? "true" : "false") << "\n";
        if (!e.paths.empty())
        {
            f << "paths = [\n";
            for (auto &p : e.paths)
            {
                auto escaped = p;
                for (auto i = escaped.find('\\'); i != std::string::npos;
                     i = escaped.find('\\', i + 2))
                    escaped.replace(i, 1, "\\\\");
                f << "    \"" << escaped << "\",\n";
            }
            f << "]\n";
        }
        f << "\n";
    }
}

static int default_trust_time()
{
    return 7 * 24 * 60;  // 7 days in minutes
}

int trust_time_minutes()
{
    auto path = prefs_path();
    if (!fs::exists(path))
        return default_trust_time();
    try
    {
        auto root = config_node::parse_file(path);
        auto val = root.get_int("trust_time_minutes");
        return val > 0 ? static_cast<int>(val) : default_trust_time();
    }
    catch (...)
    {
        return default_trust_time();
    }
}

bool is_trusted(const std::string &sha256)
{
    auto &cache = trusted_cache();
    for (auto &e : cache)
        if (e.sha256 == sha256)
            return true;
    return false;
}

void add_trusted(const std::string &sha256, const std::string &path)
{
    auto &cache = trusted_cache();
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Find existing entry or create new
    for (auto &e : cache)
    {
        if (e.sha256 == sha256)
        {
            if (!path.empty())
            {
                if (std::find(e.paths.begin(), e.paths.end(), path) == e.paths.end())
                    e.paths.push_back(path);
            }
            flush();
            return;
        }
    }

    trusted_entry e;
    e.sha256 = sha256;
    e.trusted_at = now;
    e.permanent = false;
    if (!path.empty())
        e.paths.push_back(path);
    cache.push_back(std::move(e));
    flush();
}

int sanitize()
{
    auto &cache = trusted_cache();
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto ttl_minutes = trust_time_minutes();
    auto ttl_seconds = static_cast<std::int64_t>(ttl_minutes) * 60;

    int removed = 0;
    cache.erase(
        std::remove_if(cache.begin(), cache.end(),
            [&](const trusted_entry &e) -> bool {
                if (e.permanent)
                    return false;
                auto age = now - e.trusted_at;
                if (age >= ttl_seconds)
                {
                    removed++;
                    return true;
                }
                return false;
            }),
        cache.end());

    if (removed > 0)
        flush();

    return removed;
}

} // namespace prefs
