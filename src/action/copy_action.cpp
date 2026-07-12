#include "action/copy_action.h"
#include "data/const.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/platform.h"
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

static void parse_files(const config_node &cfg, std::vector<copy_file> &out)
{
    auto arr = cfg.get_array("files");
    for (auto &elem : arr)
    {
        copy_file cf;
        cf.source = elem.get_string("source");
        auto dests = elem.get_array("dests");
        for (auto &d : dests)
            cf.dests.push_back(d.as_string());
        out.push_back(std::move(cf));
    }
}

void copy_action::parse(config_node &cfg, copy_data &d)
{
    d.defaults.source_dir = cfg.get_string("source_dir");
    if (cfg.has("fail_if_missing"))
        d.defaults.fail_if_missing = cfg.get_bool_flex("fail_if_missing");
    parse_files(cfg, d.defaults.files);

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<copy_entry> pe;

        auto sd = val.get_string("source_dir");
        if (!sd.empty()) pe.source_dir = sd;
        if (val.has("fail_if_missing"))
            pe.fail_if_missing = val.get_bool_flex("fail_if_missing");
        parse_files(val, pe.files);

        d.platform[pt] = std::move(pe);
    });
}

// Merge platform overrides on top of the defaults for the active platform.
static copy_entry resolve_entry(const copy_data &d, platform_type pt)
{
    copy_entry e = d.defaults;
    auto pit = d.platform.find(pt);
    if (pit != d.platform.end())
    {
        if (!pit->second.source_dir.empty()) e.source_dir = pit->second.source_dir;
        if (pit->second.fail_if_missing.has_value()) e.fail_if_missing = pit->second.fail_if_missing;
        if (!pit->second.files.empty()) e.files = pit->second.files;
    }
    return e;
}

static bool eff_fail(const copy_entry &e)
{
    return e.fail_if_missing.has_value() ? *e.fail_if_missing : true;
}

// Resolve a (possibly relative) path to an absolute one. root:// and cache://
// go through resolve_path; everything else is anchored to `base` (the stage
// source_dir) when given, otherwise to the project root.
static std::string abs_path(const runtime &ctx, const std::string &p, const std::string &base)
{
    if (p.find(path::root) != std::string::npos || p.find(path::cache) != std::string::npos)
        return ctx.resolve_path(p);
    if (!base.empty())
        return (fs::path(base) / p).lexically_normal().string();
    return (fs::path(ctx.root) / p).lexically_normal().string();
}

// Layer 6 path confinement for copy: sources and destinations must live within
// the project root (root://). cache:// is never allowed, and paths that escape
// the root are rejected — this cannot be bypassed with --privileged.
static bool validate_copy_path(const runtime &ctx, const std::string &raw,
                               const std::string &base, const std::string &what,
                               std::string &error)
{
    if (raw.find(path::cache) != std::string::npos)
    {
        error = "copy " + what + " must not use cache:// (project root only): " + raw;
        return false;
    }
    auto resolved = abs_path(ctx, raw, base);
    if (!security::is_in_root(ctx, resolved))
    {
        error = "copy " + what + " escapes project root (use root://): "
                + raw + " -> " + resolved;
        return false;
    }
    return true;
}

// Path relative to the project root for display (mirrors `${dst#$ROOT/}`).
static std::string root_rel(const runtime &ctx, const std::string &p)
{
    if (p.rfind(ctx.root, 0) == 0)
    {
        auto rel = p.substr(ctx.root.size());
        if (!rel.empty() && rel[0] == '/')
            rel = rel.substr(1);
        return rel;
    }
    return p;
}

// Non-error variant for is_resolved (confinement failures simply mean "not resolved").
static bool copy_path_ok(const runtime &ctx, const std::string &raw, const std::string &base)
{
    if (raw.find(path::cache) != std::string::npos)
        return false;
    return security::is_in_root(ctx, abs_path(ctx, raw, base));
}

bool copy_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    auto *d = dynamic_cast<copy_data*>(sd.data.get());
    if (!d)
        return false;

    auto e = resolve_entry(*d, ctx.platform);
    if (e.files.empty())
        return false;

    auto base = e.source_dir.empty() ? std::string() : abs_path(ctx, e.source_dir, "");
    bool fail = eff_fail(e);

    for (auto &cf : e.files)
    {
        if (!copy_path_ok(ctx, cf.source, base))
            return false;
        auto src = abs_path(ctx, cf.source, base);
        if (!fs::exists(src))
        {
            if (fail) return false;
            continue;
        }
        for (auto &dr : cf.dests)
        {
            if (!copy_path_ok(ctx, dr, ""))
                return false;
            auto dst = abs_path(ctx, dr, "");
            if (!fs::exists(dst))
                return false;
            auto h1 = platform::file_hash(src);
            auto h2 = platform::file_hash(dst);
            if (h1.empty() || h2.empty() || h1 != h2)
                return false;
        }
    }

    if (ctx.logger)
        ctx.logger->info("  nothing to do — assets already distributed");
    return true;
}

bool copy_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<copy_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no copy data";
        return false;
    }

    auto e = resolve_entry(*d, ctx.platform);
    if (e.files.empty())
    {
        error = "no files defined for copy stage " + sd.name;
        return false;
    }

    auto base = e.source_dir.empty() ? std::string() : abs_path(ctx, e.source_dir, "");
    bool fail = eff_fail(e);

    if (ctx.logger)
        ctx.logger->info("distributing " + std::to_string(e.files.size()) + " asset file(s)");

    for (auto &cf : e.files)
    {
        if (!validate_copy_path(ctx, cf.source, base, "source", error))
            return false;
        auto src = abs_path(ctx, cf.source, base);
        if (!fs::exists(src))
        {
            if (fail)
            {
                error = "copy source not found: " + src;
                return false;
            }
            if (ctx.logger)
                ctx.logger->warn("  skipping missing source: " + src);
            continue;
        }

        for (auto &dr : cf.dests)
        {
            if (!validate_copy_path(ctx, dr, "", "dest", error))
                return false;
            auto dst = abs_path(ctx, dr, "");
            std::error_code ec;
            fs::create_directories(fs::path(dst).parent_path(), ec);
            if (ec)
            {
                error = "failed to create directory for " + dst + ": " + ec.message();
                return false;
            }
            fs::copy(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = "failed to copy " + src + " -> " + dst + ": " + ec.message();
                return false;
            }
            if (ctx.logger)
                ctx.logger->info("  " + src + " -> " + root_rel(ctx, dst));
        }
    }

    return true;
}
