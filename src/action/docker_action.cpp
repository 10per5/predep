#include "action/docker_action.h"
#include "logger/logger.h"
#include "sys/process.h"
#include <filesystem>

namespace fs = std::filesystem;

bool docker_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

void docker_action::parse(config_node &cfg, docker_data &d)
{
    d.recipe = cfg.get_string("recipe");
    d.target = cfg.get_string("target");
    d.dest = cfg.get_string("dest");
}

bool docker_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<docker_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no docker data";
        return false;
    }

    auto pit = sd.platforms.find(ctx.platform);
    bool has_plat = pit != sd.platforms.end();

    auto &recipe = (has_plat && !pit->second.recipe.empty())
        ? pit->second.recipe : d->recipe;
    auto &target = (has_plat && !pit->second.target.empty())
        ? pit->second.target : d->target;
    auto &dest = (has_plat && !pit->second.dest.empty())
        ? pit->second.dest : d->dest;

    if (ctx.logger)
        ctx.logger->info("Running (docker, recipe: " + recipe + ")");

    auto cwd = action::resolve_cwd(sd, ctx);

    if (!action::confirm_build_context(sd, ctx, error))
        return false;

    // Rebase recipe path relative to the build context cwd
    auto rel_recipe = recipe;
    auto bc = action::resolve_build_context(sd, ctx);
    if (!bc.empty() && bc != "self")
    {
        auto abs_recipe = fs::absolute(fs::path(ctx.root) / recipe);
        rel_recipe = fs::relative(abs_recipe, cwd).string();

        if (ctx.logger)
            ctx.logger->info("Build context: " + cwd + ", recipe: " + rel_recipe);
    }

    auto tag = sd.name;
    auto container = tag + "-tmp";

    std::vector<std::pair<std::string, std::string>> steps = {
        {"docker build -t " + tag + " -f " + rel_recipe + " .", "docker build failed for " + sd.name},
        {"docker create --name " + container + " " + tag, "docker create failed for " + sd.name},
    };

    for (auto &[cmd, err] : steps)
    {
        if (ctx.logger)
            ctx.logger->info(cmd);
        if (process::run(process::shell(), {process::shell_cmd_flag(), cmd}, cwd) != 0)
        {
            error = err;
            return false;
        }
    }

    auto dest_path = ctx.resolve_path(dest);
    fs::create_directories(dest_path);

    auto cp_cmd = "docker cp " + container + ":" + target + " " + dest_path;
    if (ctx.logger)
        ctx.logger->info(cp_cmd);
    bool ok = process::run(process::shell(), {process::shell_cmd_flag(), cp_cmd}, cwd) == 0;
    if (!ok)
        error = "docker cp failed for " + sd.name;

    process::run(process::shell(), {process::shell_cmd_flag(), "docker rm " + container + " >/dev/null"}, cwd);
    return ok;
}
