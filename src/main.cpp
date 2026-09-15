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
#include <set>
#include <string>
#include <unordered_map>
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

    if (args.audit)
    {
        auto views = eng.stage_views();
        std::unordered_map<std::string, const stage_view*> by_name;
        for (auto &v : views) by_name[v.name] = &v;

        std::unordered_map<std::string, std::vector<std::string>> dependents;
        for (auto &v : views)
            for (auto &dep : v.depends)
                dependents[dep].push_back(v.name);

        // Informational-only risk hint (does not affect privileged behavior).
        auto danger = [](stage_type t) -> std::string {
            switch (t) {
                case stage_type::run:       return " [high: shell]";
                case stage_type::install:
                case stage_type::uninstall: return " [high: sudo]";
                case stage_type::docker:    return " [high: docker]";
                case stage_type::binary:    return " [med: binary]";
                case stage_type::premake5:
                case stage_type::cmake:     return " [med: build]";
                case stage_type::vendor:
                case stage_type::fetch:
                case stage_type::resource:  return " [med: network]";
                case stage_type::clean:     return " [med: delete]";
                default:                    return "";
            }
        };

        // `on_path` is a path stack for real cycle (back-edge) detection only;
        // shared/diamond deps are drawn as normal children each time they appear.
        std::function<void(const std::string &, const std::string &, bool,
                           std::set<std::string> &)>
            print_tree = [&](const std::string &name, const std::string &prefix,
                            bool last, std::set<std::string> &on_path)
        {
            auto it = by_name.find(name);
            if (it == by_name.end())
            {
                std::cout << prefix << (last ? "└─ " : "├─ ") << name << "  (unknown)\n";
                return;
            }
            if (on_path.count(name))
            {
                std::cout << prefix << (last ? "└─ " : "├─ ") << name
                          << "  (" << to_string(it->second->type) << ")  (cycle)\n";
                return;
            }
            on_path.insert(name);
            const auto &v = *it->second;
            std::cout << prefix << (last ? "└─ " : "├─ ") << name
                      << "  (" << to_string(v.type) << ")" << danger(v.type) << "\n";
            std::string child_prefix = prefix + (last ? "   " : "│  ");
            for (size_t i = 0; i < v.depends.size(); ++i)
                print_tree(v.depends[i], child_prefix, i + 1 == v.depends.size(),
                           on_path);
            on_path.erase(name);
        };

        std::string focus = args.command;
        std::cout << "Stage audit\n";
        if (!focus.empty() && by_name.count(focus))
        {
            std::cout << "Subtree for '" << focus << "':\n";
            std::set<std::string> on_path;
            print_tree(focus, "", true, on_path);
        }
        else
        {
            std::set<std::string> roots;
            for (auto &v : views)
                if (dependents[v.name].empty())
                    roots.insert(v.name);
            std::set<std::string> on_path;
            if (!roots.empty())
            {
                std::cout << "Entry points (stages nothing depends on):\n";
                size_t n = 0;
                for (auto &r : roots)
                    print_tree(r, "", ++n == roots.size(), on_path);
            }
            else
            {
                std::cout << "No entry points found (all stages have dependents):\n";
                for (auto &v : views)
                    std::cout << "  " << v.name << " (" << to_string(v.type) << ")" << danger(v.type) << "\n";
            }
        }
        return 0;
    }

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
