#include "action/premake5_action.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/process.h"
#include "sys/platform.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void premake5_action::parse(config_node &cfg, premake5_data &d)
{
    d.defaults.action = cfg.get_string("action", "gmake");
    d.defaults.make = cfg.get_bool_flex("make", true);
    d.defaults.strip = cfg.get_bool_flex("strip", true);
    d.defaults.target = cfg.get_string("target");
    d.defaults.project = cfg.get_string("project");
    d.defaults.config = cfg.get_string("config");

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<premake5_entry> pe;

        auto act = val.get_string("action");
        if (!act.empty()) pe.action = act;
        if (val.has("make")) pe.make = val.get_bool_flex("make");
        if (val.has("strip")) pe.strip = val.get_bool_flex("strip");
        auto tgt = val.get_string("target");
        if (!tgt.empty()) pe.target = tgt;
        auto proj = val.get_string("project");
        if (!proj.empty()) pe.project = proj;
        auto cfg_val = val.get_string("config");
        if (!cfg_val.empty()) pe.config = cfg_val;
        pe.build_context = val.get_string("build_context");

        d.platform[pt] = std::move(pe);
    });
}

bool premake5_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

bool premake5_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<premake5_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no premake5 data";
        return false;
    }

    // Resolve from defaults, overridden by platform entry
    auto project = d->defaults.project;
    auto action = d->defaults.action;
    auto make = d->defaults.make;
    auto strip = d->defaults.strip;
    auto target = d->defaults.target;
    auto config = d->defaults.config;

    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();
    if (has_plat)
    {
        auto &p = pit->second;
        if (!p.project.empty()) project = p.project;
        if (!p.action.empty()) action = p.action;
        if (p.make.has_value()) make = p.make;
        if (p.strip.has_value()) strip = p.strip;
        if (!p.target.empty()) target = p.target;
        if (!p.config.empty()) config = p.config;
    }

    if (!security::confirm_build_context(sd, d->build_context, "", ctx, error))
        return false;

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);
    auto bin_dir = ctx.cache_dir + "/bin";

    // Premake5 generation step
    auto premake_cmd = "premake5 " + action;
    ctx.logger->info(premake_cmd);
    {
        auto path_cmd = "PATH=" + bin_dir + ":$PATH " + premake_cmd;
        auto res = process::run_with_err(process::shell(), {process::shell_cmd_flag(), path_cmd}, cwd);
        if (res.code != 0)
        {
            if (!res.err.empty())
                ctx.logger->error(res.err);
            error = "premake5 failed for stage " + sd.name;
            return false;
        }
        if (!res.err.empty())
            std::cerr << res.err << "\n";
    }

    // Make step (msbuild on Windows, make elsewhere)
    if (make.value_or(false))
    {
        std::string build_cmd;
        if (ctx.platform == platform_type::windows && !project.empty())
        {
            build_cmd = "msbuild " + project + ".sln";
            if (!config.empty())
                build_cmd += " /p:Configuration=" + config;
        }
        else
        {
            build_cmd = "make -j$(nproc)";
            if (!config.empty())
                build_cmd = "make config=" + config + " -j$(nproc)";
        }

        ctx.logger->info(build_cmd);
        auto path_cmd = "PATH=" + bin_dir + ":$PATH " + build_cmd;
        auto res = process::run_with_err(process::shell(), {process::shell_cmd_flag(), path_cmd}, cwd);
        if (res.code != 0)
        {
            if (!res.err.empty())
                ctx.logger->error(res.err);
            error = "build failed for stage " + sd.name;
            return false;
        }
        if (!res.err.empty())
            std::cerr << res.err << "\n";
    }

    // Strip step
    if (strip.value_or(false))
    {
        std::vector<std::string> strip_targets;
        if (!target.empty())
            strip_targets.push_back(ctx.resolve_path(target));
        else
        {
            auto *bd = dynamic_cast<buildable_data*>(sd.data.get());
            if (bd)
                for (auto &out : bd->outputs)
                    strip_targets.push_back(ctx.resolve_path(out));
        }

        for (auto &outpath : strip_targets)
        {
            if (platform::file_exists(outpath))
            {
                auto strip_cmd = "strip " + outpath;
                ctx.logger->info(strip_cmd);
                auto path_cmd = "PATH=" + bin_dir + ":$PATH " + strip_cmd;
                auto res = process::run_with_err(process::shell(), {process::shell_cmd_flag(), path_cmd}, cwd);
                if (res.code != 0 && !res.err.empty())
                    ctx.logger->warn("strip: " + res.err);
            }
        }
    }

    return true;
}
