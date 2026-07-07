#include "action/install_action.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/platform.h"
#include "sys/process.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Tracks whether we've already shown the elevation warning during this
// install block. sudo caches credentials for ~15 min, so subsequent
// elevated calls won't prompt for a password — no need to restate the
// warning on every artifact.
static bool s_elevation_warned = false;

static std::string effective_dir(const std::string &dir, platform_type plat, const std::string &project)
{
    if (!dir.empty())
        return dir;
    if (plat == platform_type::windows && !project.empty())
        return "C:/Program Files/" + project;
    return "/usr/local/bin";
}

static bool ensure_dir(const std::string &path, Logger *logger)
{
    std::error_code ec;
    if (fs::create_directories(path, ec))
        return true;
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
#ifndef _WIN32
        auto res = process::run_with_err("sudo", {"mkdir", "-p", path});
        return res.code == 0;
#else
        return process::run_elevated("mkdir", {path}) == 0;
#endif
#else
        if (logger)
            logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
        return false;
#endif
    }

    return false;
}

static bool copy_artifact(const std::string &src, const std::string &dst, Logger *logger, bool is_self_candidate)
{
    if (fs::is_directory(src))
    {
        if (!ensure_dir(dst, logger))
            return false;

        std::error_code ec;
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
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
#ifndef _WIN32
            // Trailing /. copies contents into existing dst without nesting
            auto res = process::run_with_err("sudo", {"cp", "-r", src + "/.", dst + "/"});
            return res.code == 0;
#else
            return process::run_elevated("xcopy", {"/E", "/I", "/Y", src, dst}) == 0;
#endif
#else
            if (logger)
                logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
            return false;
#endif
        }

        return false;
    }

    // Self-install: overwriting the currently running binary.
    // Use temp-file + atomic rename to avoid ETXTBSY on Linux.
    // Only bother checking if the project name matches (can't self-install
    // a project that isn't predep).
    bool is_self = false;
#ifdef __linux__
    if (is_self_candidate)
    {
        std::error_code ec;
        if (fs::exists(dst, ec))
        {
            auto real = fs::canonical(dst, ec);
            if (!ec)
                is_self = (real == platform::exe_path());
        }
    }
#endif

    if (is_self)
    {
        auto tmp = dst + "." + path::libname + "_new";

        std::error_code ec;
        fs::copy(src, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec == std::errc::permission_denied)
        {
#ifdef ALLOW_ELEVATION
            if (logger && !s_elevation_warned)
            {
                s_elevation_warned = true;
                logger->warn("  permission denied, attempting elevation...");
            }
#ifndef _WIN32
            auto res = process::run_with_err("sudo", {"cp", src, tmp});
            if (res.code != 0) return false;
#else
            if (process::run_elevated("copy", {"/Y", src, tmp}) != 0) return false;
#endif
#else
            if (logger)
                logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
            return false;
#endif
        }
        else if (ec)
        {
            return false;
        }

        fs::rename(tmp, dst, ec);
        if (ec == std::errc::permission_denied)
        {
#ifdef ALLOW_ELEVATION
            if (logger && !s_elevation_warned)
            {
                s_elevation_warned = true;
                logger->warn("  permission denied, attempting elevation...");
            }
#ifndef _WIN32
            auto res = process::run_with_err("sudo", {"mv", tmp, dst});
            return res.code == 0;
#else
            return process::run_elevated("move", {"/Y", tmp, dst}) == 0;
#endif
#else
            if (logger)
                logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
            return false;
#endif
        }
        if (ec)
        {
            fs::remove(tmp, ec);
            return false;
        }

        return true;
    }

    std::error_code ec;
    fs::copy(src, dst, fs::copy_options::overwrite_existing, ec);
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
#ifndef _WIN32
        auto res = process::run_with_err("sudo", {"cp", src, dst});
        return res.code == 0;
#else
        return process::run_elevated("copy", {"/Y", src, dst}) == 0;
#endif
#else
        if (logger)
            logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
        return false;
#endif
    }

    return false;
}

void install_action::parse(config_node &cfg, install_data &d)
{
    d.defaults.dir = cfg.get_string("dir");

    auto arr = cfg.get_array("artifacts");
    for (auto &elem : arr)
    {
        artifact_entry ae;
        ae.source = elem.get_string("source");
        ae.dest = elem.get_string("dest");
        ae.userdir = elem.get_bool_flex("userdir");
        ae.binary = elem.get_bool_flex("binary");
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

bool install_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    auto *d = dynamic_cast<install_data*>(sd.data.get());
    if (!d)
        return false;

    auto dir = d->defaults.dir;
    auto artifacts = d->defaults.artifacts;

    auto pit = d->platform.find(ctx.platform);
    if (pit != d->platform.end())
    {
        if (!pit->second.dir.empty()) dir = pit->second.dir;
        if (!pit->second.artifacts.empty()) artifacts = pit->second.artifacts;
    }

    dir = effective_dir(dir, ctx.platform, ctx.project);
    auto install_dir = ctx.resolve_path(dir);
    if (install_dir.empty())
        return false;

    for (auto &art : artifacts)
    {
        auto src = art.source;
        auto dst = art.dest;
        if (ctx.platform == platform_type::windows && art.binary)
        {
            src += ".exe";
            dst += ".exe";
        }
        auto resolved_src = ctx.resolve_path(src);
        auto resolved_dst = (fs::path(install_dir) / dst).string();

        if (!fs::exists(resolved_src) || fs::is_directory(resolved_src))
            return false;

        if (!fs::exists(resolved_dst))
            return false;

        auto src_hash = platform::file_hash(resolved_src);
        auto dst_hash = platform::file_hash(resolved_dst);
        if (src_hash.empty() || dst_hash.empty() || src_hash != dst_hash)
            return false;
    }

    if (ctx.logger)
        ctx.logger->info("  nothing to do — files already up to date");
    return true;
}

bool install_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<install_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no install data";
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
    // Clear any stale sudo credential cache from outside predep
    process::run_with_err("sudo", {"-K"});
#endif

    auto install_dir = ctx.resolve_path(dir);
    if (install_dir.empty())
    {
        error = "install dir is empty";
        return false;
    }

    if (!ensure_dir(install_dir, ctx.logger))
    {
        error = "failed to create install directory: " + install_dir;
        return false;
    }

    if (ctx.logger)
        ctx.logger->info("  install prefix: " + install_dir);

    for (auto &art : artifacts)
    {
        if (ctx.platform == platform_type::windows && art.binary)
        {
            art.source += ".exe";
            art.dest += ".exe";
        }
        auto src = ctx.resolve_path(art.source);
        auto dst = (fs::path(install_dir) / art.dest).string();

        if (!fs::exists(src))
        {
            error = "artifact not found: " + src;
            return false;
        }

        auto parent = fs::path(dst).parent_path().string();
        if (!parent.empty() && parent != install_dir)
        {
            if (ctx.logger)
                ctx.logger->info("  creating " + parent);
            if (!ensure_dir(parent, ctx.logger))
            {
                error = "failed to create parent directory: " + parent;
                return false;
            }
        }

        if (ctx.logger)
            ctx.logger->info("  " + src + " -> " + dst);

        if (!copy_artifact(src, dst, ctx.logger, ctx.project == path::libname))
        {
            error = "failed to copy " + src + " to " + dst;
            return false;
        }
    }

#ifndef _WIN32
    if (symlink && !artifacts.empty() && !ctx.project.empty())
    {
        auto default_bin = std::string("/usr/local/bin");
        if (install_dir != default_bin)
        {
            auto link_target = install_dir + "/" + artifacts[0].dest;
            auto link_path = default_bin + "/" + ctx.project;

            if (!ensure_dir(default_bin, ctx.logger))
            {
                error = "failed to create " + default_bin;
                return false;
            }

            std::error_code ec;
            fs::remove(link_path, ec);
            if (ec == std::errc::permission_denied)
            {
#ifdef ALLOW_ELEVATION
                process::run_with_err("sudo", {"rm", "-f", link_path});
#else
                if (ctx.logger)
                    ctx.logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
                return false;
#endif
            }

            std::error_code ec2;
            fs::create_symlink(link_target, link_path, ec2);
            if (ec2 == std::errc::permission_denied)
            {
#ifdef ALLOW_ELEVATION
                auto res = process::run_with_err("sudo", {"ln", "-sf", link_target, link_path});
                if (res.code != 0)
                {
                    error = "failed to create symlink " + link_path + " -> " + link_target + ": " + res.err;
                    return false;
                }
#else
                if (ctx.logger)
                    ctx.logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
                return false;
#endif
            }
            else if (ec2)
            {
                error = "failed to create symlink " + link_path + " -> " + link_target + ": " + ec2.message();
                return false;
            }

            if (ctx.logger)
                ctx.logger->info("  symlink " + link_path + " -> " + link_target);
        }
    }
#else
    (void)symlink;
#endif

    // Write manifest for uninstall (elevation still active here)
    {
        auto manifest_path = (fs::path(install_dir) / path::manifest).string();
        std::string manifest;
        for (auto &art : artifacts)
        {
            auto src = ctx.resolve_path(art.source);
            auto marker = art.userdir ? 'U' : (fs::is_directory(src) ? 'D' : 'F');
            manifest += marker;
            manifest += ':';
            manifest += art.dest;
            manifest += '\n';
        }

        if (!manifest.empty())
        {
            // Write to a temp file in a writable location, then move into place
            auto tmp = ctx.cache_dir + "/.predep-manifest.tmp";
            {
                std::ofstream ofs(tmp);
                if (ofs)
                    ofs << manifest;
            }
            std::error_code ec;
            fs::rename(tmp, manifest_path, ec);
            if (ec)
            {
                // Direct write fallback
                std::ofstream ofs(manifest_path);
                if (ofs)
                    ofs << manifest;
                if (!ofs)
                {
#ifdef ALLOW_ELEVATION
#ifndef _WIN32
                    // Write via sudo cp from temp
                    process::run_with_err("sudo", {"cp", tmp, manifest_path});
#else
                    process::run_elevated("copy", {"/Y", tmp, manifest_path});
#endif
#else
                    if (ctx.logger)
                        ctx.logger->error("  failed to write manifest and predep was built without ALLOW_ELEVATION");
#endif
                }
            }
            // Clean up temp
            fs::remove(tmp, ec);
        }
    }

#if defined(ALLOW_ELEVATION) && !defined(_WIN32)
    // Revoke sudo credentials so no subsequent stage inherits them
    process::run_with_err("sudo", {"-K"});
#endif

    return true;
}
