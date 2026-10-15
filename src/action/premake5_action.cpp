#include "action/premake5_action.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/process.h"
#include "sys/platform.h"
#include <cctype>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

// premake5 flags are concatenated into a command that is executed through
// process::shell() (i.e. /bin/sh -c "<cmd>"). A flag containing spaces,
// quotes, or shell metacharacters ( ; & | $ ` > < ( ) { } [ ] * ? ! \ ) is
// therefore a command-injection vector. We accept ONLY single tokens shaped
// like `--word-args-like-this[=value]`: a leading '-' followed by characters
// from the safe set [A-Za-z0-9._:/+=-]. Anything else is discarded (never
// evaluated) by the caller.
bool is_safe_flag(const std::string &f)
{
    if (f.empty() || f.front() != '-')
        return false;
    for (unsigned char c : f)
    {
        bool ok = std::isalnum(c) || c == '-' || c == '_' || c == '.' ||
                  c == '/' || c == ':' || c == '=' || c == '+';
        if (!ok)
            return false;   // whitespace or shell metacharacter
    }
    return true;
}

} // namespace

std::vector<std::string> premake5_action::sanitize_flags(const std::vector<std::string> &flags)
{
    std::vector<std::string> out;
    for (auto &f : flags)
        if (is_safe_flag(f))
            out.push_back(f);
    return out;
}

void premake5_action::parse(config_node &cfg, premake5_data &d)
{
    d.defaults.action = cfg.get_string("action", "gmake");
    d.defaults.make = cfg.get_bool_flex("make", true);
    d.defaults.strip = cfg.get_bool_flex("strip", true);
    d.defaults.target = cfg.get_string("target");
    d.defaults.project = cfg.get_string("project");
    d.defaults.config = cfg.get_string("config");
    for (auto &f : cfg.get_array("flags"))
        d.defaults.flags.push_back(f.as_string());

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
        for (auto &f : val.get_array("flags"))
            pe.flags.push_back(f.as_string());
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
    auto flags = d->defaults.flags;

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
        if (!p.flags.empty()) flags = p.flags;   // platform override replaces (consistent with scalars)
    }

    if (!security::confirm_build_context(sd, d->build_context, "", ctx, error))
        return false;

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);
    auto bin_dir = ctx.cache_dir + "/bin";

    // Premake5 generation step
    // Security: premake_cmd is executed via /bin/sh -c, so any flag with spaces
    // or shell metacharacters is a command-injection vector. Keep only single
    // --word-args-like-this tokens; discard the rest and warn the user.
    auto premake_cmd = "premake5 " + action;
    for (auto &f : flags)
    {
        if (is_safe_flag(f))
            premake_cmd += " " + f;
        else
            ctx.logger->warn("flag '" + f + "' was discarded for security: premake5 flags "
                             "must be a single --word-args-like-this token with no spaces "
                             "or shell metacharacters. Spaces are not currently allowed.");
    }
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
