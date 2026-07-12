#include "action/clean_action.h"
#include "logger/logger.h"
#include "sys/platform.h"
#include <filesystem>

namespace fs = std::filesystem;

void clean_action::parse(config_node &cfg, clean_data &d)
{
    auto targets = cfg.get_array("targets");
    for (auto &elem : targets)
        d.targets.push_back(elem.as_string());
    auto arr = cfg.get_array("paths");
    for (auto &elem : arr)
        d.paths.push_back(elem.as_string());
}

bool clean_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

// Collect all cleanable paths from a stage descriptor.
static void collect_paths(const stage_desc &sd, std::vector<std::string> &paths)
{
    // download types (vendor / fetch / resource)
    if (auto *dd = dynamic_cast<download_data*>(sd.data.get()))
    {
        if (!dd->clean)
            return;
        for (auto &p : dd->clean_paths)
            paths.push_back(p);
        return;
    }

    auto *bd = dynamic_cast<buildable_data*>(sd.data.get());
    if (!bd || !bd->clean)
        return;

    // Declared outputs
    for (auto &out : bd->outputs)
        paths.push_back(out);

    // Extra clean paths
    for (auto &p : bd->clean_paths)
        paths.push_back(p);

    // premake5 target
    if (sd.type == stage_type::premake5)
    {
        auto *pd = dynamic_cast<premake5_data*>(sd.data.get());
        if (pd && !pd->defaults.target.empty())
            paths.push_back(pd->defaults.target);
    }

    // docker dest
    if (sd.type == stage_type::docker)
    {
        auto *dd = dynamic_cast<docker_data*>(sd.data.get());
        if (dd && !dd->defaults.dest.empty())
            paths.push_back(dd->defaults.dest);
    }

    // package artifact sources
    if (sd.type == stage_type::package)
    {
        auto *pd = dynamic_cast<package_data*>(sd.data.get());
        if (pd)
        {
            for (auto &art : pd->artifacts)
                paths.push_back(art.source);
        }
    }
}

bool clean_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *cd = dynamic_cast<clean_data*>(sd.data.get());
    if (!cd)
    {
        error = "stage " + sd.name + " has no clean data";
        return false;
    }

    std::vector<std::string> paths;

    // Collect paths from targets (not resolved as build deps)
    if (ctx.stages)
    {
        for (auto &name : cd->targets)
        {
            auto it = ctx.stages->find(name);
            if (it == ctx.stages->end())
            {
                if (ctx.logger)
                    ctx.logger->warn("  clean: target '" + name + "' not found, skipping");
                continue;
            }
            collect_paths(it->second, paths);
        }
    }

    // Add the clean stage's own extra paths
    for (auto &p : cd->paths)
        paths.push_back(p);

    if (paths.empty())
    {
        if (ctx.logger)
            ctx.logger->info("  nothing to clean for stage: " + sd.name);
        return true;
    }

    if (ctx.logger)
        ctx.logger->info("  cleaning " + std::to_string(paths.size()) + " path(s)");

    for (auto &p : paths)
    {
        auto resolved = ctx.resolve_path(p);
        std::error_code ec;

        if (fs::is_regular_file(resolved, ec))
        {
            fs::remove(resolved, ec);
            if (!ec && ctx.logger)
                ctx.logger->debug("    removed file: " + resolved);
        }
        else if (fs::is_directory(resolved, ec))
        {
            fs::remove_all(resolved, ec);
            if (!ec && ctx.logger)
                ctx.logger->debug("    removed directory: " + resolved);
        }
        else if (ec)
        {
            if (ctx.logger)
                ctx.logger->debug("    skipped (not found): " + resolved);
        }
    }

    return true;
}
