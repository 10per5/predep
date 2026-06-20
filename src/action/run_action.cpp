#include "action/run_action.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/process.h"
#include <filesystem>
#include <iostream>

bool run_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

void run_action::parse(config_node &cfg, run_data &d)
{
    auto arr = cfg.get_array("commands");
    for (auto &elem : arr)
        d.defaults.commands.push_back(elem.as_string());

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<run_entry> pe;

        auto cmds = val.get_array("commands");
        for (auto &c : cmds)
            pe.commands.push_back(c.as_string());
        pe.build_context = val.get_string("build_context");

        d.platform[pt] = std::move(pe);
    });
}

bool run_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<run_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no run data";
        return false;
    }

    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();

    auto &cmds = (has_plat && !pit->second.commands.empty())
        ? pit->second.commands : d->defaults.commands;

    if (cmds.empty())
    {
        error = "no commands defined for stage " + sd.name
                + " on platform " + platform::to_string(ctx.platform);
        return false;
    }

    if (has_plat)
        ctx.logger->info("Running (platform: " + platform::to_string(ctx.platform) + ")");

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);

    if (!security::confirm_build_context(sd, bc, cwd, ctx, error))
        return false;

    auto bin_dir = ctx.cache_dir + "/bin";
    for (auto &cmd : cmds)
    {
        ctx.logger->info(cmd);
        auto path_cmd = "PATH=" + bin_dir + ":$PATH " + cmd;
        auto res = process::run_with_err(process::shell(), {process::shell_cmd_flag(), path_cmd}, cwd);
        if (res.code != 0)
        {
            if (!res.err.empty())
                ctx.logger->error(res.err);
            error = "command failed for stage " + sd.name;
            return false;
        }
        if (!res.err.empty())
            std::cerr << res.err << "\n";
    }

    return true;
}
