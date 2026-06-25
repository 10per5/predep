#include "cli/args.h"
#include "engine/engine.h"
#include "cli/discovery.h"
#include "logger/logger.h"
#include "logger/prompter.h"
#include "security/security.h"
#include "sys/platform.h"
#include "data/stage.h"
#include "data/const.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    auto args = parse_args(argc, argv);

    if (args.version)
    {
        std::cout << PREDEP_VERSION << "\n";
        return 0;
    }

    auto logger = make_logger(args.format, args.debug);
    auto prompter = make_prompter(args.privileged);

    auto root = project_root(args.parent_limit);

    if (args.config_path.empty())
    {
        args.config_path = find_config(root, *logger);
        if (args.config_path.empty())
        {
            logger->error("no predep.toml or predep.lua found in " + root);
            return 1;
        }
    }

    if (args.debug)
        logger->debug("root=" + root + " config=" + args.config_path);

    bool is_toml = args.config_path.ends_with(".toml");

    engine eng;
    bool loaded = is_toml ? eng.load_toml(args.config_path) : eng.load_lua(args.config_path);
    if (!loaded)
    {
        logger->error("failed to load config: " + eng.last_error());
        return 1;
    }

    runtime ctx;
    ctx.root = root;
    ctx.cache_dir = platform::cache_dir();
    ctx.target_os = args.target_os;
    ctx.platform = platform::from_string(args.platform_override.empty() ? args.target_os : args.platform_override);
    ctx.max_concurrency = args.jobs;
    ctx.privileged = args.privileged;
    ctx.config_sha = args.privileged_sha;
    ctx.logger = logger.get();
    ctx.prompter = prompter.get();

    {
        std::string sudo_err;
        if (!security::check_root_sudo(ctx, sudo_err))
        {
            logger->error(sudo_err);
            return 1;
        }
    }

    auto main_name = eng.main_stage();

    if (args.list)
    {
        auto names = eng.stage_names();
        std::vector<std::string> root, ns_stages;
        for (auto &n : names)
        {
            if (n.find("::") != std::string::npos)
                ns_stages.push_back(n);
            else
                root.push_back(n);
        }
        std::sort(root.begin(), root.end());
        std::sort(ns_stages.begin(), ns_stages.end());

        std::cout << "Available stages:\n";
        for (auto &n : root)
            std::cout << "  " << n << "\n";
        if (!ns_stages.empty())
        {
            std::cout << "\n";
            std::string cur_ns;
            for (auto &n : ns_stages)
            {
                auto pos = n.find("::");
                auto ns = n.substr(0, pos);
                auto name = n.substr(pos + 2);
                if (ns != cur_ns)
                {
                    cur_ns = ns;
                    std::cout << "  [" << ns << "]\n";
                }
                std::cout << "    " << name << "\n";
            }
        }
        if (!main_name.empty())
            std::cout << "\nMain stage: " << main_name << "\n";
        return 0;
    }

    auto stage_name = args.command.empty() ? main_name : args.command;
    if (stage_name.empty())
    {
        logger->error("no stage specified and no main stage defined in config");
        logger->info("Use --list to see available stages");
        return 1;
    }

    if (!eng.has_stage(stage_name))
    {
        logger->error("unknown stage '" + stage_name + "'");
        std::cout << "  Available: ";
        for (auto &n : eng.stage_names())
            std::cout << n << " ";
        std::cout << "\n";
        return 1;
    }

    if (!eng.resolve(stage_name, ctx))
    {
        logger->error(eng.last_error());
        return 1;
    }

    logger->info("Stage '" + stage_name + "' resolved successfully");
    return 0;
}
