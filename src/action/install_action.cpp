#include "action/install_action.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/platform.h"
#include "sys/process.h"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Tracks whether we've already shown the elevation warning during this
// install block. sudo caches credentials for ~15 min, so subsequent
// elevated calls won't prompt for a password — no need to restate the
// warning on every artifact.
static bool s_elevation_warned = false;

// Per-artifact permissions: artifact-level value wins over the install-level
// default (root `[install]` `chmod` / `chown_user`).
static int effective_mode(const artifact_entry &ae, int default_mode)
{
    return ae.mode >= 0 ? ae.mode : default_mode;
}

static bool effective_chown_user(const artifact_entry &ae, bool default_chown)
{
    return ae.chown_user || default_chown;
}

static std::string effective_dir(const std::string &dir, platform_type plat, const std::string &project)
{
    if (!dir.empty())
        return dir;
    if (plat == platform_type::windows && !project.empty())
        return "C:/Program Files/" + project;
    return "/usr/local/bin";
}

static bool set_file_mode(const std::string &path, int mode, Logger *logger)
{
#ifndef _WIN32
    if (::chmod(path.c_str(), static_cast<mode_t>(mode)) == 0)
        return true;

    if (errno == EACCES || errno == EPERM)
    {
#ifdef ALLOW_ELEVATION
        if (logger && !s_elevation_warned)
        {
            s_elevation_warned = true;
            logger->warn("  permission denied, attempting elevation...");
        }
        char oct[16];
        std::snprintf(oct, sizeof(oct), "0%03o", mode);
        return process::run_with_err("sudo", {"chmod", oct, path}).code == 0;
#else
        if (logger)
            logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
        return false;
#endif
    }

    return false;
#else
    (void)path;
    (void)mode;
    (void)logger;
    return true;
#endif
}

static bool chown_to_user(const std::string &path, Logger *logger)
{
#ifndef _WIN32
    uid_t uid = ::getuid();
    gid_t gid = platform::install_group();
    // Sets both owner and group in one call. The owner may change the group
    // without elevation only to a group they belong to; otherwise the sudo
    // fallback below repairs it.
    if (::chown(path.c_str(), uid, gid) == 0)
        return true;

    if (errno == EACCES || errno == EPERM)
    {
#ifdef ALLOW_ELEVATION
        if (logger && !s_elevation_warned)
        {
            s_elevation_warned = true;
            logger->warn("  permission denied, attempting elevation...");
        }
        auto uid_gid = std::to_string(uid) + ":" + std::to_string(gid);
        return process::run_with_err("sudo", {"chown", uid_gid, path}).code == 0;
#else
        if (logger)
            logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
        return false;
#endif
    }

    return false;
#else
    (void)path;
    (void)logger;
    return true;
#endif
}

// Recursively transfers ownership of path and everything under it to the
// invoking user and the chown_user group. Tries a direct walk first; on any
// EACCES/EPERM it falls back to a single `sudo chown -R`. Used for directory
// artifacts so the whole tree stays user-writable for later re-installs.
static bool tree_chown_to_user(const std::string &path, Logger *logger)
{
#ifndef _WIN32
    uid_t uid = ::getuid();
    gid_t gid = platform::install_group();
    bool needs_sudo = false;

    auto apply = [&](const char *p) -> bool
    {
        if (::chown(p, uid, gid) == 0)
            return true;
        if (errno == EACCES || errno == EPERM)
        {
            needs_sudo = true;
            return true;
        }
        return false;
    };

    if (!apply(path.c_str()))
        return false;

    std::error_code ec;
    fs::recursive_directory_iterator it(path, ec);
    fs::recursive_directory_iterator end;
    if (ec)
        return false;
    for (; it != end; it.increment(ec))
    {
        if (ec)
            return false;
        if (!apply(it->path().c_str()))
            return false;
    }

    if (!needs_sudo)
        return true;

#ifdef ALLOW_ELEVATION
    if (logger && !s_elevation_warned)
    {
        s_elevation_warned = true;
        logger->warn("  permission denied, attempting elevation...");
    }
    auto uid_gid = std::to_string(uid) + ":" + std::to_string(gid);
    return process::run_with_err("sudo", {"chown", "-R", uid_gid, path}).code == 0;
#else
    if (logger)
        logger->error("  permission denied and predep was built without ALLOW_ELEVATION");
    return false;
#endif
#else
    (void)path;
    (void)logger;
    return true;
#endif
}

// True when path and every entry under it are owned by the invoking user with
// the chown_user group. Always true on Windows (ownership transfer is a no-op
// there).
static bool tree_matches_ownership(const std::string &path)
{
#ifndef _WIN32
    if (!platform::file_matches_ownership(path))
        return false;
    std::error_code ec;
    fs::recursive_directory_iterator it(path, ec);
    fs::recursive_directory_iterator end;
    if (ec)
        return false;
    for (; it != end; it.increment(ec))
    {
        if (ec)
            return false;
        if (!platform::file_matches_ownership(it->path().string()))
            return false;
    }
    return true;
#else
    (void)path;
    return true;
#endif
}

// Recursively compares a source directory against a destination directory.
// Returns true when every file under src exists at the same relative path
// under dst with identical content (size fast-path, then sha256). Extra files
// in dst are ignored — install doesn't clean the destination.
static bool directories_match(const std::string &src, const std::string &dst)
{
    std::error_code ec;
    fs::recursive_directory_iterator it(src, ec);
    fs::recursive_directory_iterator end;
    if (ec)
        return false;

    for (; it != end; it.increment(ec))
    {
        if (ec)
            return false;

        auto rel = fs::relative(it->path(), src, ec);
        if (ec)
            return false;
        auto counterpart = (fs::path(dst) / rel).string();

        std::error_code sec;
        if (it->is_directory(sec))
        {
            if (sec || !fs::is_directory(counterpart, sec) || sec)
                return false;
            continue;
        }
        if (sec || !fs::exists(counterpart, sec) || sec)
            return false;
        if (fs::is_directory(counterpart, sec))
            return false;

        auto s1 = fs::file_size(it->path(), sec);
        if (sec)
            return false;
        auto s2 = fs::file_size(counterpart, sec);
        if (sec || s1 != s2)
            return false;

        if (platform::file_hash(it->path().string()) != platform::file_hash(counterpart))
            return false;
    }
    return true;
}

// Resolves an artifact's source and destination paths, applying the Windows
// .exe suffix for binary artifacts. `rel` (optional) receives the destination
// relative to the install dir, for the uninstall manifest.
static void artifact_paths(const artifact_entry &ae, const std::string &install_dir,
                           runtime &ctx, std::string &src, std::string &dst, std::string *rel = nullptr)
{
    auto s = ae.source;
    auto d = ae.dest;
    if (ctx.platform == platform_type::windows && ae.binary)
    {
        s += ".exe";
        d += ".exe";
    }
    src = ctx.resolve_path(s);
    dst = (fs::path(install_dir) / d).string();
    if (rel)
        *rel = d;
}

// True when the artifact is already installed with matching content and the
// declared permissions/ownership. Shared by the stage-level skip in
// is_resolved() and by resolve() to avoid re-copying unchanged artifacts
// (re-copying a root-owned, unchanged file would prompt for elevation for no
// reason, e.g. when only a sibling artifact needs a refresh).
static bool artifact_up_to_date(const artifact_entry &ae, const std::string &src,
                                const std::string &dst, int default_mode, bool default_chown)
{
    if (!fs::exists(src))
        return false;

    if (fs::is_directory(src))
    {
        if (!fs::is_directory(dst) || !directories_match(src, dst))
            return false;
        if (effective_chown_user(ae, default_chown) && !tree_matches_ownership(dst))
            return false;
        return true;
    }

    if (!fs::exists(dst) || fs::is_directory(dst))
        return false;

    auto src_hash = platform::file_hash(src);
    auto dst_hash = platform::file_hash(dst);
    if (src_hash.empty() || dst_hash.empty() || src_hash != dst_hash)
        return false;

    auto mode = effective_mode(ae, default_mode);
    if (mode >= 0)
    {
        auto cur = platform::file_mode(dst);
        if (cur != -1 && cur != mode)
            return false;
    }
    if (effective_chown_user(ae, default_chown) && !platform::file_matches_ownership(dst))
        return false;
    return true;
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

    if (cfg.get_octal_mode("chmod", d.defaults.mode) < 0)
        d.defaults.mode = -1;
    d.defaults.chown_user = cfg.get_bool_flex("chown_user", d.defaults.chown_user);

    auto arr = cfg.get_array("artifacts");
    for (auto &elem : arr)
    {
        artifact_entry ae;
        ae.source = elem.get_string("source");
        ae.dest = elem.get_string("dest");
        ae.userdir = elem.get_bool_flex("userdir");
        ae.binary = elem.get_bool_flex("binary");
        if (elem.get_octal_mode("chmod", ae.mode) < 0)
            ae.mode = -1;
        ae.chown_user = elem.get_bool_flex("chown_user");
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
            ae.binary = elem.get_bool_flex("binary");
            if (elem.get_octal_mode("chmod", ae.mode) < 0)
                ae.mode = -1;
            ae.chown_user = elem.get_bool_flex("chown_user");
            pe.artifacts.push_back(ae);
        }

        if (val.has("symlink")) pe.symlink = val.get_bool_flex("symlink");
        if (val.get_octal_mode("chmod", pe.mode) < 0)
            pe.mode = -1;
        pe.chown_user = val.get_bool_flex("chown_user");
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
    auto default_mode = d->defaults.mode;
    auto default_chown = d->defaults.chown_user;

    auto pit = d->platform.find(ctx.platform);
    if (pit != d->platform.end())
    {
        if (!pit->second.dir.empty()) dir = pit->second.dir;
        if (!pit->second.artifacts.empty()) artifacts = pit->second.artifacts;
        if (pit->second.mode >= 0) default_mode = pit->second.mode;
        if (pit->second.chown_user) default_chown = true;
    }

    dir = effective_dir(dir, ctx.platform, ctx.project);
    auto install_dir = ctx.resolve_path(dir);
    if (install_dir.empty())
        return false;

    for (auto &art : artifacts)
    {
        std::string src, dst;
        artifact_paths(art, install_dir, ctx, src, dst);
        if (!artifact_up_to_date(art, src, dst, default_mode, default_chown))
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
    auto default_mode = d->defaults.mode;
    auto default_chown = d->defaults.chown_user;

    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();
    if (has_plat)
    {
        if (!pit->second.dir.empty()) dir = pit->second.dir;
        if (!pit->second.artifacts.empty()) artifacts = pit->second.artifacts;
        symlink = pit->second.symlink;
        if (pit->second.mode >= 0) default_mode = pit->second.mode;
        if (pit->second.chown_user) default_chown = true;
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
        std::string src, dst;
        artifact_paths(art, install_dir, ctx, src, dst);

        if (!fs::exists(src))
        {
            error = "artifact not found: " + src;
            return false;
        }

        // Skip artifacts already installed with matching content and declared
        // permissions/ownership. Re-copying an unchanged root-owned file would
        // prompt for elevation even though nothing about it changed (e.g. when
        // only a nested folder of a sibling artifact is missing).
        if (artifact_up_to_date(art, src, dst, default_mode, default_chown))
        {
            if (ctx.logger)
                ctx.logger->info("  " + dst + " already up to date");
            continue;
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

        // Apply declared permissions/ownership (artifact-level wins over the
        // install-level default). Runs inside the sudo credential window, so
        // elevated chmod/chown can repair files copied as root.
        if (fs::is_directory(dst))
        {
            // Directory artifacts: chown_user applies recursively so the whole
            // tree stays user-writable for later re-installs. chmod is not
            // applied to dirs — a file-oriented mode (e.g. 0644) would make
            // the tree untraversable.
            if (effective_chown_user(art, default_chown) && !tree_chown_to_user(dst, ctx.logger))
            {
                error = "failed to change ownership of " + dst;
                return false;
            }
            continue;
        }

        auto mode = effective_mode(art, default_mode);
        if (mode >= 0 && !set_file_mode(dst, mode, ctx.logger))
        {
            error = "failed to set permissions on " + dst;
            return false;
        }
        if (effective_chown_user(art, default_chown) && !chown_to_user(dst, ctx.logger))
        {
            error = "failed to change ownership of " + dst;
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

            // Skip if the link already points at the target — avoids a sudo
            // prompt on re-installs where nothing about the link changed.
            bool link_ok = false;
            {
                std::error_code sec;
                if (fs::is_symlink(link_path, sec) && !sec)
                {
                    auto cur = fs::read_symlink(link_path, sec);
                    link_ok = !sec && cur == fs::path(link_target);
                }
            }
            if (link_ok)
            {
                if (ctx.logger)
                    ctx.logger->info("  symlink " + link_path + " already up to date");
            }
            else
            {
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
            std::string src, dst, rel;
            artifact_paths(art, install_dir, ctx, src, dst, &rel);
            auto marker = art.userdir ? 'U' : (fs::is_directory(src) ? 'D' : 'F');
            manifest += marker;
            manifest += ':';
            manifest += rel;
            manifest += '\n';
        }

        if (!manifest.empty())
        {
            // Skip rewriting when the manifest is unchanged — the install dir
            // is often root-owned, so a rewrite would fall back to sudo and
            // prompt even when only an unrelated artifact changed.
            std::ifstream ifs(manifest_path);
            std::string existing((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
            if (existing == manifest)
            {
                if (ctx.logger)
                    ctx.logger->info("  manifest already up to date");
            }
            else
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
    }

#if defined(ALLOW_ELEVATION) && !defined(_WIN32)
    // Revoke sudo credentials so no subsequent stage inherits them
    process::run_with_err("sudo", {"-K"});
#endif

    return true;
}
