#include "action/docker_action.h"
#include "logger/logger.h"
#include "security/security.h"
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
    d.defaults.recipe = cfg.get_string("recipe");
    d.defaults.target = cfg.get_string("target");
    d.defaults.dest = cfg.get_string("dest");

    auto build_args = cfg.get_table("build_args");
    if (build_args)
    {
        build_args.for_each([&](const std::string &k, const config_node &v)
        {
            d.defaults.build_args[k] = v.as_string();
        });
    }

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<docker_entry> pe;

        pe.recipe = val.get_string("recipe");
        pe.target = val.get_string("target");
        pe.dest = val.get_string("dest");
        pe.build_context = val.get_string("build_context");

        auto pa = val.get_table("build_args");
        if (pa)
        {
            pa.for_each([&](const std::string &k, const config_node &v)
            {
                pe.build_args[k] = v.as_string();
            });
        }

        d.platform[pt] = std::move(pe);
    });
}

bool docker_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<docker_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no docker data";
        return false;
    }

    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();

    auto &recipe = (has_plat && !pit->second.recipe.empty())
        ? pit->second.recipe : d->defaults.recipe;
    auto &target = (has_plat && !pit->second.target.empty())
        ? pit->second.target : d->defaults.target;
    auto &dest = (has_plat && !pit->second.dest.empty())
        ? pit->second.dest : d->defaults.dest;

    auto build_args = d->defaults.build_args;
    if (has_plat)
    {
        for (auto &[k, v] : pit->second.build_args)
            build_args[k] = v;
    }

    if (ctx.logger)
        ctx.logger->info("Running (docker, recipe: " + recipe + ")");

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);

    if (!security::confirm_build_context(sd, bc, cwd, ctx, error))
        return false;

    auto rel_recipe = recipe;
    if (!bc.empty() && bc != "self")
    {
        auto abs_recipe = fs::absolute(fs::path(ctx.root) / recipe);
        rel_recipe = fs::relative(abs_recipe, cwd).string();

        if (ctx.logger)
            ctx.logger->info("Build context: " + cwd + ", recipe: " + rel_recipe);
    }

    auto tag = sd.name;
    auto container = tag + "-tmp";

    std::string build_cmd = "docker build";
    for (auto &[k, v] : build_args)
        build_cmd += " --build-arg " + k + "=" + v;
    build_cmd += " -t " + tag + " -f " + rel_recipe + " .";

    std::vector<std::pair<std::string, std::string>> steps = {
        {build_cmd, "docker build failed for " + sd.name},
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

    auto cp_source = container + ":" + target;
    if (!target.empty() && target.back() == '/')
        cp_source += '.';
    auto cp_cmd = "docker cp " + cp_source + " " + dest_path;
    if (ctx.logger)
        ctx.logger->info(cp_cmd);
    bool ok = process::run(process::shell(), {process::shell_cmd_flag(), cp_cmd}, cwd) == 0;
    if (!ok)
        error = "docker cp failed for " + sd.name;

    process::run(process::shell(), {process::shell_cmd_flag(), "docker rm " + container + " >/dev/null"}, cwd);
    return ok;
}
