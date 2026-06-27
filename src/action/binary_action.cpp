#include "action/binary_action.h"
#include "logger/logger.h"
#include "sys/platform.h"
#include "sys/process.h"
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

void binary_action::parse(config_node &cfg, binary_data &d)
{
    d.binary_name = cfg.get_string("binary");

    // args: free-form argv tokens (DANGEROUS — no structure)
    auto args_arr = cfg.get_array("args");
    for (auto &a : args_arr)
        d.defaults.args.push_back(a.as_string());

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<binary_entry> pe;

        auto pparams = val.get_table("params");
        if (pparams)
        {
            pparams.for_each([&](const std::string &pk, const config_node &pv)
            {
                pe.params[pk] = pv.as_string();
            });
        }

        auto pargs = val.get_array("args");
        for (auto &a : pargs)
            pe.args.push_back(a.as_string());
        pe.build_context = val.get_string("build_context");

        d.platform[pt] = std::move(pe);
    });
}

bool binary_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

static void build_argv(const binary_entry &entry,
                       std::vector<std::string> &out)
{
    // params → --key value (structured, but unschema'd → DANGEROUS)
    for (auto &[k, v] : entry.params)
    {
        out.push_back("--" + k);
        out.push_back(v);
    }
    // args → free-form tokens (DANGEROUS — no validation)
    for (auto &a : entry.args)
        out.push_back(a);
}

bool binary_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<binary_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no binary data";
        return false;
    }

    if (d->binary_name.empty())
    {
        error = "stage " + sd.name + " is type 'binary' but no 'binary' field specified";
        return false;
    }

    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();

    // Merge platform overrides: each field independently overridable
    binary_entry resolved;
    if (has_plat)
    {
        if (!pit->second.params.empty())
            resolved.params = pit->second.params;
        else
            resolved.params = d->defaults.params;
        if (!pit->second.args.empty())
            resolved.args = pit->second.args;
        else
            resolved.args = d->defaults.args;
    }
    else
    {
        resolved = d->defaults;
    }

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);

    std::vector<std::string> argv;
    build_argv(resolved, argv);

    auto bin_dir = (fs::path(ctx.cache_dir) / "bin").string();

    // Check cache://bin first, then system PATH
    auto bin_name = platform::exe_name(d->binary_name);
    auto path_cmd = (fs::path(bin_dir) / bin_name).string();
    auto res = process::run_with_err(path_cmd, argv, cwd);
    if (res.code == -1)
    {
        // Fall back to PATH lookup (bare name; execvp/CreateProcessA handle PATHEXT)
        res = process::run_with_err(d->binary_name, argv, cwd);
    }

    if (res.code == -1)
    {
        error = "binary '" + d->binary_name + "' not found in " + bin_dir
                + " or PATH for stage " + sd.name;
        return false;
    }

    if (res.code != 0)
    {
        error = "binary '" + d->binary_name + "' exited with code " + std::to_string(res.code)
                + " for stage " + sd.name;
        if (!res.err.empty())
            error += "\n" + res.err;
        return false;
    }

    if (has_plat)
        ctx.logger->info("Running (platform: " + platform::to_string(ctx.platform) + ")");

    return true;
}
