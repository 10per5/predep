#include "security/security.h"
#include "cfg/prefs.h"
#include "data/const.h"
#include "data/stage.h"
#include "logger/console.h"
#include "logger/prompter.h"
#include "sys/platform.h"
#include <set>

#ifndef _WIN32
#include <unistd.h>
#endif

bool security::check_root_sudo(const runtime &ctx, std::string &error)
{
#ifdef _WIN32
    (void)ctx;
    return true;
#else
    auto uid = getuid();
    if (uid != 0)
        return true;

    auto *sudo_user = std::getenv("SUDO_USER");
    auto *sudo_uid  = std::getenv("SUDO_UID");

    if (sudo_user || sudo_uid)
    {
        error = "Do not run predep with sudo.\n"
                "  If elevated privileges are needed, predep will prompt\n"
                "  for sudo automatically during install/uninstall stages.\n"
                "  Run as a normal user instead.";
    }
    else
    {
        error = "Do not run predep as root.\n"
                "  Use a normal user account. If elevated privileges are\n"
                "  needed, predep will prompt for sudo automatically\n"
                "  during install/uninstall stages.";
    }

    if (ctx.privileged && !console::is_tty())
        return true;

    return false;
#endif
}

bool security::check_path_safety(
    const std::unordered_map<std::string, stage_desc> &stages,
    const std::vector<std::string> &config_files,
    runtime &ctx,
    std::string &error)
{
    struct path_issue {
        std::string stage;
        std::string kind;
        std::string resolved;
        path::check_result result;
    };
    std::vector<path_issue> issues;

    for (auto &[name, sd] : stages)
    {
        // fetch/vendor/resource dest — privilege::install (root:// or cache://)
        if (auto *dd = dynamic_cast<download_data*>(sd.data.get()))
        {
            for (auto &e : dd->entries)
            {
                if (e.dest.empty())
                    continue;
                auto resolved = ctx.resolve_path(e.dest);
                auto cr = path::check(resolved, ctx.root, ctx.cache_dir, privilege::install);
                if (cr != path::check_result::ok)
                    issues.push_back({name, "download dest", resolved, cr});
            }
        }

        // docker dest — privilege::build (root:// only)
        if (auto *dd = dynamic_cast<docker_data*>(sd.data.get()))
        {
            auto check_dest = [&](const std::string &d, const std::string &label) {
                if (d.empty()) return;
                auto resolved = ctx.resolve_path(d);
                auto cr = path::check(resolved, ctx.root, ctx.cache_dir, privilege::build);
                if (cr != path::check_result::ok)
                    issues.push_back({name, label, resolved, cr});
            };
            check_dest(dd->defaults.dest, "docker dest");
            for (auto &[pt, pe] : dd->platform)
                check_dest(pe.dest, "docker dest (" + platform::to_string(pt) + ")");
        }

        // package artifact source — privilege::build (root:// only)
        if (auto *pd = dynamic_cast<package_data*>(sd.data.get()))
        {
            for (auto &art : pd->artifacts)
            {
                auto resolved = ctx.resolve_path(art.source);
                auto cr = path::check(resolved, ctx.root, ctx.cache_dir, privilege::build);
                if (cr != path::check_result::ok)
                    issues.push_back({name, "artifact source", resolved, cr});
            }
        }

        // install artifact source — must be under project/cache (build scope)
        if (auto *id = dynamic_cast<install_data*>(sd.data.get()))
        {
            for (auto &art : id->defaults.artifacts)
            {
                auto resolved = ctx.resolve_path(art.source);
                auto cr = path::check(resolved, ctx.root, ctx.cache_dir, privilege::build);
                if (cr != path::check_result::ok)
                    issues.push_back({name, "install artifact source", resolved, cr});
            }
        }
    }

    // When --privileged is used, verify the main config SHA matches
    if (ctx.privileged)
    {
        if (ctx.config_sha.empty())
        {
            error = "--privileged requires the SHA256 of the config file.\n"
                    "  Usage: predep --privileged <sha256> <stage>";
            return false;
        }
        auto &main_cfg = config_files[0];
        auto main_sha = platform::file_hash(main_cfg);
        if (main_sha.empty())
        {
            error = "Failed to hash: " + main_cfg;
            return false;
        }
        if (main_sha != ctx.config_sha)
        {
            error = "Config SHA does not match --privileged argument.\n"
                    "  Provided: " + ctx.config_sha + "\n"
                    "  Expected: " + main_sha + "  " + main_cfg;
            return false;
        }
    }

    if (issues.empty())
        return true;

    // Separate config errors from privilege-required paths
    std::string config_errors;
    std::string privilege_paths;
    for (auto &iss : issues)
    {
        auto line = "  [" + iss.stage + "] " + iss.kind + ": " + iss.resolved + "\n";
        if (iss.result == path::check_result::config_error)
            config_errors += line;
        else
            privilege_paths += line;
    }

    if (!config_errors.empty())
    {
        error = "Path configuration errors (these are not fixable with --privileged):\n" + config_errors;
        return false;
    }

    error = "Paths outside project or cache directory (requires --privileged):\n" + privilege_paths;

    if (!ctx.privileged)
    {
        error += "Use --privileged <config-sha256> to allow system-path access.";
        return false;
    }

    privileged_countdown(ctx.logger);
    return true;
}

bool security::check_run_stages(
    const std::unordered_map<std::string, stage_desc> &stages,
    const std::vector<std::string> &config_files,
    runtime &ctx,
    std::string &error)
{
    // Build a set of config files that are already trusted
    std::set<std::string> trusted_files;
    for (auto &cfg : config_files)
    {
        auto sha = platform::file_hash(cfg);
        if (!sha.empty() && prefs::is_trusted(sha))
            trusted_files.insert(cfg);
    }

    // Collect run/binary entries grouped by source file
    struct run_entry_info {
        std::string source;
        std::string description;
    };
    std::vector<run_entry_info> untrusted_entries;

    for (auto &[name, sd] : stages)
    {
        bool is_run = sd.type == stage_type::run;
        bool is_binary = sd.type == stage_type::binary;
        if (!is_run && !is_binary)
            continue;

        // Skip if this stage's source file is already trusted
        if (trusted_files.count(sd.source_file))
            continue;

        if (is_run)
        {
            auto *rd = dynamic_cast<run_data*>(sd.data.get());
            if (rd)
            {
                for (auto &cmd : rd->defaults.commands)
                    untrusted_entries.push_back({sd.source_file, "  * [" + name + "] " + cmd});
                auto pit = rd->platform.find(ctx.platform);
                if (pit != rd->platform.end())
                {
                    for (auto &cmd : pit->second.commands)
                        untrusted_entries.push_back({sd.source_file, "  * [" + name + ":" + platform::to_string(ctx.platform) + "] " + cmd});
                }
            }
        }
        else if (is_binary)
        {
            auto *bd = dynamic_cast<binary_data*>(sd.data.get());
            if (bd && !bd->binary_name.empty())
            {
                auto &entry = bd->defaults;
                auto pit = bd->platform.find(ctx.platform);
                bool has_plat = pit != bd->platform.end();

                auto line = std::string("  * [") + name + "] " + bd->binary_name;
                auto &params = (has_plat && !pit->second.params.empty())
                    ? pit->second.params : entry.params;
                auto &args = (has_plat && !pit->second.args.empty())
                    ? pit->second.args : entry.args;

                if (!params.empty())
                {
                    line += "\n      params:";
                    for (auto &[k, v] : params)
                        line += " --" + k + " " + v;
                }
                if (!args.empty())
                {
                    line += "\n      args:";
                    for (auto &a : args)
                        line += " " + a;
                }
                untrusted_entries.push_back({sd.source_file, line});
            }
        }
    }

    if (untrusted_entries.empty())
        return true;

    if (!ctx.prompter)
    {
        error = "non-interactive mode: use --privileged to override";
        return false;
    }

    // Collect unique untrusted file paths
    std::set<std::string> untrusted_files;
    for (auto &e : untrusted_entries)
        untrusted_files.insert(e.source);

    std::string body;
    body += "The following configuration files are not yet trusted:\n";
    for (auto &f : untrusted_files)
        body += "    " + f + "\n";
    body += "\nPlease review these files before confirming.\n\nCommands that will be executed:\n";
    for (auto &e : untrusted_entries)
        body += e.description + "\n";

    std::string prompt_error;
    if (!ctx.prompter->confirm_or_abort("RUN STAGE WARNING", body, prompt_error, safety_level::dangerous))
    {
        error = prompt_error;
        return false;
    }

    // Trust all untrusted config files
    for (auto &f : untrusted_files)
    {
        auto sha = platform::file_hash(f);
        if (!sha.empty())
            prefs::add_trusted(sha, f);
    }

    return true;
}

bool security::confirm_build_context(
    const stage_desc &sd,
    const std::string &bc,
    const std::string &cwd,
    runtime &ctx,
    std::string &error)
{
    if (bc.empty() || bc == "self" || bc == "parent")
        return true;

    if (ctx.prompter)
    {
        std::string body =
            "  Stage '" + sd.name + "' uses custom build context:\n"
            "    " + bc + " -> " + cwd + "\n"
            "  Files outside the stage directory will be\n"
            "  visible to the build process.";
        if (!ctx.prompter->confirm_or_abort("BUILD CONTEXT WARNING", body, error, safety_level::dangerous))
            return false;
    }

    return true;
}
