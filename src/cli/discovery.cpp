#include "cli/discovery.h"
#include "logger/logger.h"
#include "sys/platform.h"
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

std::string project_root(int parent_limit)
{
    auto *env = std::getenv("PREDEP_DIR");
    if (env)
        return env;

    auto start = fs::current_path();
    auto current = start;
    for (int depth = 0; ; depth++)
    {
        if (fs::exists(current / "predep.toml") || fs::exists(current / "predep.lua"))
            return current.string();

        if (parent_limit >= 0 && depth >= parent_limit)
            break;

        auto parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }

    return start.string();
}

std::string find_config(const std::string &root, Logger &logger)
{
    for (auto &name : {"predep.toml", "predep.lua"})
    {
        auto path = root + "/" + name;
        if (platform::file_exists(path))
        {
            logger.info("using config: " + path);
            return path;
        }
    }
    return {};
}
