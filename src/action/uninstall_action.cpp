#include "action/uninstall_action.h"
#include "logger/logger.h"
#include "logger/prompter.h"
#include "security/security.h"
#include "sys/platform.h"
#include "sys/process.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static bool s_elevation_warned = false;

static std::string effective_dir(const std::string &dir, platform_type plat, const std::string &project)
{
    if (!dir.empty())
        return dir;
    if (plat == platform_type::windows && !project.empty())
        return "C:/Program Files/" + project;
    return "/usr/local/bin";
}

static bool remove_with_sudo(const std::string &path, Logger *logger)
{
    bool is_dir = fs::is_directory(path);

    std::error_code ec;
    if (is_dir)
    {
        fs::remove_all(path, ec);
        if (!ec)
            return true;
    }
    else
    {
        if (fs::remove(path, ec))
            return true;
    }
    if (!ec)
        return true;

    if (ec == std::errc::permission_denied)
    {
#ifdef ALLOW_ELEVATION
        if (logger && !s_elevation_warned)
        {
            s_elevation_warned = true;
            logger->warn("  permission denied, attempting elevation...");
        }
        if (is_dir)
        {
#ifndef _WIN32
            auto res = process::run_with_err("sudo", {"rm", "-rf", path});
            return res.code == 0;
#else
            return process::run_elevated("rmdir", {"/s", "/q", path}) == 0;
#endif
        }
        else
        {
#ifndef _WIN32
            auto res = process::run_with_err("sudo", {"rm", "-f", path});
            return res.code == 0;
#else
            return process::run_elevated("del", {"/f", "/q", path}) == 0;
#endif
        }
#else
        if (logger)
            logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
        return false;
#endif
    }

    return false;
}

void uninstall_action::parse(config_node &cfg, uninstall_data &d)
{
    d.defaults.dir = cfg.get_string("dir");

    auto arr = cfg.get_array("artifacts");
    for (auto &elem : arr)
    {
        artifact_entry ae;
        ae.source = elem.get_string("source");
        ae.dest = elem.get_string("dest");
        ae.userdir = elem.get_bool_flex("userdir");
        d.defaults.artifacts.push_back(ae);
    }

    if (cfg.has("symlink"))
        d.defaults.symlink = cfg.get_bool_flex("symlink");

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<install_entry> pe;

        auto dir = val.get_string("dir");
        if (!dir.empty()) pe.dir = dir;

        auto arts = val.get_array("artifacts");
        for (auto &elem : arts)
        {
            artifact_entry ae;
            ae.source = elem.get_string("source");
            ae.dest = elem.get_string("dest");
            ae.userdir = elem.get_bool_flex("userdir");
            pe.artifacts.push_back(ae);
        }

        if (val.has("symlink")) pe.symlink = val.get_bool_flex("symlink");
        pe.build_context = val.get_string("build_context");

        d.platform[pt] = std::move(pe);
    });
}

bool uninstall_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

bool uninstall_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<uninstall_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no uninstall data";
        return false;
    }

    auto dir = d->defaults.dir;
    auto artifacts = d->defaults.artifacts;
    auto symlink = d->defaults.symlink;

    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();
    if (has_plat)
    {
        if (!pit->second.dir.empty()) dir = pit->second.dir;
        if (!pit->second.artifacts.empty()) artifacts = pit->second.artifacts;
        symlink = pit->second.symlink;
    }

    dir = effective_dir(dir, ctx.platform, ctx.project);
    s_elevation_warned = false;

    if (!security::confirm_build_context(sd, d->build_context, "", ctx, error))
        return false;

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    (void)action::resolve_cwd(bc, ctx);

#if defined(ALLOW_ELEVATION) && !defined(_WIN32)
    process::run_with_err("sudo", {"-K"});
#endif

    auto install_dir = ctx.resolve_path(dir);
    if (install_dir.empty())
    {
        error = "install dir is empty";
        return false;
    }

    auto manifest_path = (fs::path(install_dir) / path::manifest).string();
    std::ifstream ifs(manifest_path);
    if (!ifs)
    {
        error = "no manifest found at " + manifest_path + "; cannot uninstall safely";
        return false;
    }

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.size() < 2 || line[1] != ':')
            continue;
        char marker = line[0];
        auto rel = line.substr(2);
        auto dst = (fs::path(install_dir) / rel).string();

        if (marker == 'U')
        {
            std::string prompt_err;
            if (ctx.prompter &&
                !ctx.prompter->confirm_or_abort("User Directory",
                    "Delete user directory '" + rel + "'?",
                    prompt_err, safety_level::dangerous))
            {
                if (ctx.logger)
                    ctx.logger->info("  skipped " + rel);
                continue;
            }
        }

        if (remove_with_sudo(dst, ctx.logger))
        {
            if (ctx.logger && !fs::exists(dst))
                ctx.logger->info("  removed " + rel);
        }
        else if (ctx.logger)
        {
            ctx.logger->warn("  removal failed: " + rel);
        }
    }

    // Clean up empty ancestors of each root artifact parent
    for (auto &art : artifacts)
    {
        auto parent = (fs::path(install_dir) / art.dest).parent_path();
        while (parent != install_dir && parent.has_parent_path())
        {
            std::error_code ec;
            if (fs::is_empty(parent, ec) && !ec)
            {
                if (!remove_with_sudo(parent.string(), ctx.logger))
                    break;
                if (ctx.logger)
                    ctx.logger->info("  removed empty " + parent.string());
            }
            else
                break;
            parent = parent.parent_path();
        }
    }

#ifndef _WIN32
    if (symlink && !ctx.project.empty())
    {
        auto link_path = std::string("/usr/local/bin/") + ctx.project;
        std::error_code ec;
        if (fs::exists(link_path, ec) && fs::is_symlink(link_path))
            remove_with_sudo(link_path, ctx.logger);
    }
#endif

    // Check if install dir is deletable: empty except for .predep-manifest
    // and not a well-known system directory.
    auto is_system_dir = [](const std::string &p) -> bool
    {
        auto c = fs::weakly_canonical(p);
        static const std::vector<std::string> protected_dirs = {
            "/", "/usr", "/usr/local", "/usr/local/bin",
            "/opt", "/etc", "/var", "/home",
            "/bin", "/sbin", "/lib", "/lib64",
#ifdef _WIN32
            "C:\\", "C:\\Windows", "C:\\Program Files",
#endif
        };
        for (auto &d : protected_dirs)
        {
            std::error_code ec;
            if (c == fs::weakly_canonical(d, ec))
                return true;
        }
        return false;
    };

    {
        std::error_code ec;
        bool only_manifest = true;
        for (auto &entry : fs::directory_iterator(install_dir, ec))
        {
            if (entry.path().filename() != path::manifest)
            {
                only_manifest = false;
                break;
            }
        }
        if (!ec && only_manifest && !is_system_dir(install_dir))
        {
            remove_with_sudo((fs::path(install_dir) / path::manifest).string(), ctx.logger);
            remove_with_sudo(install_dir, ctx.logger);
            if (ctx.logger && !fs::exists(install_dir))
                ctx.logger->info("  removed " + install_dir);
        }
    }

#if defined(ALLOW_ELEVATION) && !defined(_WIN32)
    process::run_with_err("sudo", {"-K"});
#endif

    return true;
}
