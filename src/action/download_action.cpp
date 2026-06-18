#include "action/download_action.h"
#include "cfg/config_loader.h"
#include "sys/download.h"
#include "sys/extract.h"
#include "logger/logger.h"
#include "sys/platform.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

download_action::vendor_entry_vars download_action::resolve_vendor_vars(
    const vendor_entry &ve,
    const std::map<std::string, std::string> &stage_vars,
    const runtime &ctx)
{
    vendor_entry_vars r;
    r.vars = stage_vars;
    for (auto &[k, v] : ve.vars)
        r.vars[k] = v;

    auto set_default = [&](const std::string &k, const std::string &v)
    { if (r.vars.find(k) == r.vars.end()) r.vars[k] = v; };
    set_default("PLATFORM", ctx.platform);
    set_default("ARCH", platform::arch());
    set_default("OS", ctx.target_os);

    {
        auto arch = platform::arch();
        auto cpu = arch;
        if (arch == "x86_64") cpu = "amd64";
        else if (arch == "aarch64") cpu = "arm64";
        set_default("CPU", cpu);
    }
#ifdef _WIN32
    set_default("EXE_SUFFIX", ".exe");
#else
    set_default("EXE_SUFFIX", "");
#endif

    r.url = config_loader::interpolate(ve.url, r.vars);
    r.output_name = config_loader::interpolate(
        ve.output_name.empty() ? ve.name : ve.output_name, r.vars);
    r.base = ctx.resolve_path(ve.dest);
    auto slash = r.url.rfind('/');
    r.fname = (slash != std::string::npos) ? r.url.substr(slash + 1) : ve.name;
    return r;
}

void download_action::parse(config_node &cfg, download_data &d)
{
    auto parse_vec = [&](const std::string &key,
                         std::vector<vendor_entry> &vec,
                         const std::string &default_dest)
    {
        auto arr = cfg.get_array(key);
        for (auto &elem : arr)
        {
            vendor_entry ve;
            ve.name = elem.get_string("name");
            ve.url = elem.get_string("url");
            ve.dest = elem.get_string("dest", default_dest);
            ve.sha256 = elem.get_string("sha256");
            ve.extract = elem.get_bool("extract");
            ve.create_directory = elem.get_bool("create_directory");
            ve.output_name = elem.get_string("output_name");

            elem.for_each([&](const std::string &ek, const config_node &ev)
            {
                if (!ev.is_string())
                    return;
                if (ek != "name" && ek != "url" && ek != "dest" &&
                    ek != "sha256" && ek != "type" &&
                    ek != "output_name" &&
                    ek != "extract" && ek != "create_directory")
                    ve.vars[ek] = ev.as_string();
            });

            vec.push_back(ve);
        }
    };

    parse_vec("vendor", d.vendors, "root://vendor/");
    parse_vec("binary", d.binaries, "cache://bin/");
    parse_vec("resource", d.resources, "root://resources/");
}

bool download_action::check_vendor_vec(
    const download_data &d,
    const std::map<std::string, std::string> &stage_vars,
    const runtime &ctx) const
{
    auto check = [&](const std::vector<vendor_entry> &vec) -> bool
    {
        for (auto &ve : vec)
        {
            auto rv = resolve_vendor_vars(ve, stage_vars, ctx);
            if (ve.extract)
            {
                auto extract_dir = ve.create_directory ? rv.base + "/" + ve.name : rv.base;
                auto expected = extract_dir + "/" + rv.output_name;
                if (!platform::file_exists(expected))
                    return false;
            }
            else
            {
                auto expected = rv.base + "/" + rv.output_name;
                if (!platform::file_exists(expected))
                    return false;
                if (!ve.sha256.empty() && platform::file_hash(expected) != ve.sha256)
                    return false;
            }
        }
        return true;
    };

    if (!d.vendors.empty() && !check(d.vendors))
        return false;
    if (!d.binaries.empty() && !check(d.binaries))
        return false;
    if (!d.resources.empty() && !check(d.resources))
        return false;
    return true;
}

bool download_action::resolve_vendor_vec(
    download_data &d,
    const std::map<std::string, std::string> &stage_vars,
    runtime &ctx,
    std::string &error,
    const std::string &type)
{
    auto res = [&](const std::vector<vendor_entry> &vec) -> bool
    {
        for (auto &ve : vec)
        {
            auto rv = resolve_vendor_vars(ve, stage_vars, ctx);

            if (ve.extract)
            {
                auto extract_dir = ve.create_directory ? rv.base + "/" + ve.name : rv.base;
                auto archive_path = ctx.cache_dir + "/archives/" + rv.fname;
                fs::create_directories(fs::path(archive_path).parent_path());

                bool need_download = true;
                if (platform::file_exists(archive_path))
                    if (ve.sha256.empty() || platform::file_hash(archive_path) == ve.sha256)
                        need_download = false;

                if (need_download)
                {
                    ctx.logger->info("Downloading " + type + " " + ve.name + " from " + rv.url);
                    if (!download::download_verify(rv.url, archive_path, ve.sha256))
                    {
                        error = type + " download failed for " + ve.name;
                        return false;
                    }
                }
                else
                    ctx.logger->info(type + " " + ve.name + " archive cached");

                fs::create_directories(extract_dir);
                bool ok = false;
                if (rv.fname.ends_with(".tar.gz") || rv.fname.ends_with(".tgz"))
                    ok = extract::tar_gz(archive_path, extract_dir);
                else if (rv.fname.ends_with(".zip"))
                    ok = extract::zip(archive_path, extract_dir);
                else
                    ok = true;

                if (!ok)
                {
                    error = "extraction failed for " + ve.name;
                    return false;
                }

                if (rv.output_name != ve.name)
                {
                    auto src = extract_dir + "/" + ve.name;
                    auto dst = extract_dir + "/" + rv.output_name;
                    if (fs::exists(src) && !fs::exists(dst))
                        fs::rename(src, dst);
                }
            }
            else
            {
                auto dest_path = ve.create_directory
                    ? rv.base + "/" + ve.name + "/" + rv.fname
                    : rv.base + "/" + rv.fname;

                auto parent = fs::path(dest_path).parent_path();
                fs::create_directories(parent);

                if (platform::file_exists(dest_path))
                {
                    if (!ve.sha256.empty() && platform::file_hash(dest_path) != ve.sha256)
                    {
                        ctx.logger->info(ve.name + " SHA256 mismatch, re-downloading");
                        fs::remove(dest_path);
                    }
                    else
                    {
                        ctx.logger->info(type + " " + ve.name + " already up to date");
                        continue;
                    }
                }

                ctx.logger->info("Downloading " + type + " " + ve.name + " from " + rv.url);

                if (!download::download_verify(rv.url, dest_path, ve.sha256))
                {
                    error = type + " download failed for " + ve.name;
                    return false;
                }

                if (rv.output_name != ve.name)
                {
                    auto dst = rv.base + "/" + rv.output_name;
                    if (!fs::exists(dst))
                        fs::rename(dest_path, dst);
                }
            }
        }
        return true;
    };

    if (!d.vendors.empty() && !res(d.vendors))
        return false;
    if (!d.binaries.empty() && !res(d.binaries))
        return false;
    if (!d.resources.empty() && !res(d.resources))
        return false;
    return true;
}

bool download_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    auto *d = dynamic_cast<const download_data*>(sd.data.get());
    if (d && check_vendor_vec(*d, sd.vars, ctx))
        return true;

    return check_outputs(sd, ctx);
}

bool download_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<download_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no download data";
        return false;
    }

    bool has_entries = !d->vendors.empty()
                    || !d->binaries.empty()
                    || !d->resources.empty();

    if (!has_entries)
    {
        error = "stage " + sd.name + " has type '" + sd.type + "' but no entries defined";
        return false;
    }

    return resolve_vendor_vec(*d, sd.vars, ctx, error, sd.type);
}
