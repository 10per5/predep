#include "action/run_action.h"
#include "logger/logger.h"
#include "sys/process.h"
#include <filesystem>
#include <iostream>

// Always rebuild — no output-file staleness check.
// TODO: add timestamp-based comparison (source newer than output) to skip
//       rebuild when nothing has changed.
bool run_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    return false;
}

void run_action::parse(config_node &cfg, run_data &d)
{
    auto arr = cfg.get_array("commands");
    for (auto &elem : arr)
        d.commands.push_back(elem.as_string());

    auto native = cfg.get_array("native");
    for (auto &elem : native)
        d.commands.push_back(elem.as_string());
}

bool run_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<run_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no run data";
        return false;
    }

    auto pit = sd.platforms.find(ctx.platform);
    bool has_plat = pit != sd.platforms.end();

    auto &cmds = (has_plat && !pit->second.commands.empty())
        ? pit->second.commands : d->commands;

    if (cmds.empty())
    {
        error = "no commands defined for stage " + sd.name
                + " on platform " + ctx.platform;
        return false;
    }

    if (has_plat)
        ctx.logger->info("Running (platform: " + ctx.platform + ")");

    auto cwd = action::resolve_cwd(sd, ctx);

    if (!action::confirm_build_context(sd, ctx, error))
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
