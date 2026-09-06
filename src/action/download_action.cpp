#include "action/download_action.h"
#include "cfg/config_loader.h"
#include "sys/download.h"
#include "sys/extract.h"
#include "logger/logger.h"
#include "sys/platform.h"
#include "sys/process.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>

static void show_progress(const std::string &label, size_t downloaded, size_t total)
{
    if (total == 0) return;
    int pct = static_cast<int>(downloaded * 100 / total);
    int bar = 40;
    int pos = pct * bar / 100;

    std::string line = "  " + label + ": [";
    for (int i = 0; i < bar; i++)
        line += (i < pos ? '=' : (i == pos && pos < bar ? '>' : ' '));
    line += "] ";
    if (pct < 10) line += "  ";
    else if (pct < 100) line += " ";
    line += std::to_string(pct) + "%";

    if (line.size() < 62)
        line.append(62 - line.size(), ' ');

    std::cerr << '\r' << line << std::flush;

    if (downloaded >= total)
        std::cerr << "\n  " << label << ": done.\n";
}

// Renders one git progress segment (e.g. "Receiving objects:  42% (12/29)")
// as a bar. Returns true once a bar has been drawn.
static bool git_show_progress(const std::string &name, const std::string &seg, bool drawn)
{
    static const std::regex prog(R"(^(\w[\w ]*):\s+(\d+)% \((\d+)/(\d+)\))");
    std::smatch m;
    if (!std::regex_search(seg, m, prog))
        return drawn;

    auto phase = m[1].str();
    long cur = std::stol(m[3]);
    long tot = std::stol(m[4]);
    if (tot == 0)
        return drawn;

    int pct = static_cast<int>(cur * 100 / tot);
    int bar = 40;
    int pos = pct * bar / 100;

    std::string line = "  " + name + " " + phase + ": [";
    for (int i = 0; i < bar; i++)
        line += (i < pos ? '=' : (i == pos && pos < bar ? '>' : ' '));
    line += "] ";
    if (pct < 10) line += "  ";
    else if (pct < 100) line += " ";
    line += std::to_string(pct) + "%";

    if (line.size() < 79)
        line.append(79 - line.size(), ' ');

    std::cerr << '\r' << line << std::flush;
    return true;
}

namespace fs = std::filesystem;

download_action::resolved_entry download_action::resolve_entry(
    const fetch_entry &fe,
    const std::map<std::string, std::string> &stage_vars,
    const runtime &ctx)
{
    resolved_entry r;
    r.vars = stage_vars;
    for (auto &[k, v] : fe.vars)
        r.vars[k] = v;

    auto set_default = [&](const std::string &k, const std::string &v)
    { if (r.vars.find(k) == r.vars.end()) r.vars[k] = v; };
    set_default("PLATFORM", platform::to_string(ctx.platform));
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

    auto archext = (ctx.platform == platform_type::windows) ? std::string("zip") : std::string("tar.gz");
    set_default("ARCHEXT", archext);

    r.url = config_loader::interpolate(fe.url, r.vars);
    r.output_name = config_loader::interpolate(
        fe.output_name.empty() ? fe.name : fe.output_name, r.vars);
    r.base = ctx.resolve_path(fe.dest);
    auto slash = r.url.rfind('/');
    r.fname = (slash != std::string::npos) ? r.url.substr(slash + 1) : fe.name;
    return r;
}

static bool is_git_entry(const fetch_entry &fe) { return !fe.repo.empty(); }

static bool git_run(const std::vector<std::string> &args, const std::string &cwd,
                    runtime &ctx, const std::string &what, std::string &error)
{
    auto res = process::run_with_err("git", args, cwd);
    if (res.code != 0)
    {
        if (!res.err.empty())
            ctx.logger->error(res.err);
        error = "git " + what + " failed";
        return false;
    }
    return true;
}

// Pull a git repo and pin it to an exact ref (SHA or tag). Idempotent via a
// `.predepgit` marker that records the resolved ref.
bool download_action::resolve_git(fetch_entry &fe,
                                  const std::map<std::string, std::string> &stage_vars,
                                  runtime &ctx, std::string &error)
{
    auto rv = download_action::resolve_entry(fe, stage_vars, ctx);
    auto ref = config_loader::interpolate(fe.ref, rv.vars);
    auto repo = config_loader::interpolate(fe.repo, rv.vars);
    auto dest = fs::path(rv.base);
    auto dest_str = dest.string();
    auto marker = (dest / ".predepgit").string();

    if (platform::file_exists(marker))
    {
        std::ifstream f(marker);
        std::string stored;
        std::getline(f, stored);
        if (stored == ref)
        {
            ctx.logger->info("vendor " + fe.name + " already at " + ref);
            return true;
        }
        ctx.logger->info("vendor " + fe.name + " ref changed, re-fetching");
        fs::remove_all(dest);
    }

    fs::create_directories(dest.parent_path());

    ctx.logger->info("Cloning " + fe.name + " from " + repo + " (" + ref + ")");

    std::vector<std::string> args = {"clone", "--progress"};
    if (fe.depth > 0)
    {
        args.push_back("--depth");
        args.push_back(std::to_string(fe.depth));
    }
    if (fe.submodules)
        args.push_back("--recursive");
    args.push_back(repo);
    args.push_back(dest_str);

    // git only draws progress on a TTY; --progress forces it so we can parse
    // the stderr stream and render our own bar.
    std::string pending;
    bool drawn = false;
    auto on_chunk = [&](const std::string &chunk)
    {
        pending += chunk;
        size_t pos;
        while ((pos = pending.find_first_of("\r\n")) != std::string::npos)
        {
            auto seg = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            drawn = git_show_progress(fe.name, seg, drawn);
        }
    };

    auto res = process::run_with_err_stream("git", args,
                                            dest.parent_path().string(), on_chunk);
    drawn = git_show_progress(fe.name, pending, drawn);
    if (drawn)
        std::cerr << std::endl;

    if (res.code != 0)
    {
        if (!res.err.empty())
            ctx.logger->error(res.err);
        error = "git clone " + fe.name + " failed";
        return false;
    }

    // Checkout the pinned ref. A full 40-hex SHA may be absent from a shallow
    // clone, so fetch it explicitly first when checkout fails.
    auto checkout = process::run_with_err("git", {"-C", dest_str, "checkout", ref}, "");
    if (checkout.code != 0)
    {
        if (!git_run({"-C", dest_str, "fetch", "origin", ref}, "", ctx,
                     "fetch " + fe.name, error))
            return false;
        if (!git_run({"-C", dest_str, "checkout", ref}, "", ctx,
                     "checkout " + fe.name, error))
            return false;
    }

    {
        std::ofstream f(marker);
        f << ref << "\n";
    }
    ctx.logger->info("vendor " + fe.name + " pinned to " + ref);
    return true;
}

fetch_entry download_action::parse_entry(
    config_node &elem, const std::string &default_dest)
{
    fetch_entry fe;
    fe.fetch_type = elem.get_string("fetch-type");
    fe.name = elem.get_string("name");
    fe.url = elem.get_string("url");
    fe.dest = elem.get_string("dest", default_dest);
    fe.sha256 = elem.get_string("sha256");
    fe.extract = elem.get_bool_flex("extract");
    fe.create_directory = elem.get_bool_flex("create_directory");
    fe.output_name = elem.get_string("output_name");

    // Git source acquisition (alternative to url/sha256 archive download)
    fe.repo = elem.get_string("repo");
    fe.ref = elem.get_string("ref");
    fe.depth = (int)elem.get_int("depth", 0);
    fe.submodules = elem.get_bool_flex("submodules", false);
    fe.builder = elem.get_string("builder");

    auto parse_str_or_arr = [&](const std::string &key, std::vector<std::string> &out)
    {
        auto arr = elem.get_array(key);
        if (!arr.empty())
        {
            for (auto &a : arr)
                out.push_back(a.as_string());
        }
        else
        {
            auto s = elem.get_string(key);
            if (!s.empty())
                out.push_back(s);
        }
    };
    parse_str_or_arr("include", fe.include);
    parse_str_or_arr("exclude", fe.exclude);

    auto ver = elem.get_string("version");
    if (!ver.empty())
        fe.vars["version"] = ver;

    auto var_table = elem.get_table("variables");
    if (var_table)
    {
        var_table.for_each([&](const std::string &vk, const config_node &vv)
        {
            if (vv.is_string())
                fe.vars[vk] = vv.as_string();
        });
    }
    return fe;
}

void download_action::parse(config_node &cfg, download_data &d)
{
    d.fetch_type = cfg.get_string("fetch-type");

    auto asset_arr = cfg.get_array("assets");
    for (auto &a : asset_arr)
        d.assets.push_back(a.as_string());

    auto parse_vec = [&](const std::string &key,
                         std::vector<fetch_entry> &vec,
                         const std::string &default_dest)
    {
        auto arr = cfg.get_array(key);
        for (auto &elem : arr)
            vec.push_back(parse_entry(elem, default_dest));
    };

    parse_vec("vendor", d.entries, "root://vendor/");
    parse_vec("binary", d.entries, "cache://bin/");
    parse_vec("resource", d.entries, "root://resources/");
}

bool download_action::check_entries(
    const download_data &d,
    const std::map<std::string, std::string> &stage_vars,
    const runtime &ctx) const
{
    for (auto &fe_orig : d.entries)
    {
        auto fe = fe_orig.for_platform(ctx.platform);
        auto rv = resolve_entry(fe, stage_vars, ctx);

        if (is_git_entry(fe))
        {
            auto marker = (fs::path(rv.base) / ".predepgit").string();
            if (!platform::file_exists(marker))
                return false;
            std::ifstream f(marker);
            std::string stored;
            std::getline(f, stored);
            auto ref = config_loader::interpolate(fe.ref, rv.vars);
            if (stored != ref)
                return false;
            continue;
        }

        if (fe.extract)
        {
            auto extract_dir = fe.create_directory ? (fs::path(rv.base) / fe.name).string() : rv.base;
            auto csum_path = (fs::path(extract_dir) / (fe.name + ".predepsum")).string();
            if (platform::file_exists(csum_path))
            {
                bool match = fe.sha256.empty();
                if (!fe.sha256.empty())
                {
                    std::ifstream f(csum_path);
                    std::string stored;
                    std::getline(f, stored);
                    match = (stored == fe.sha256);
                }
                if (match)
                    continue;
            }
            auto expected = (fs::path(extract_dir) / rv.output_name).string();
            if (!platform::file_exists(expected))
                return false;
        }
        else
        {
            auto expected = (fs::path(rv.base) / rv.output_name).string();
            if (!platform::file_exists(expected))
                return false;
            if (!fe.sha256.empty() && platform::file_hash(expected) != fe.sha256)
                return false;
        }
    }
    return true;
}

bool download_action::resolve_entries(
    download_data &d,
    const std::map<std::string, std::string> &stage_vars,
    runtime &ctx,
    std::string &error,
    const std::string &type)
{
    for (auto &fe_orig : d.entries)
    {
        auto fe = fe_orig.for_platform(ctx.platform);
        auto rv = resolve_entry(fe, stage_vars, ctx);

        if (is_git_entry(fe))
        {
            if (!resolve_git(fe, stage_vars, ctx, error))
                return false;
            continue;
        }

        if (fe.extract)
        {
            auto extract_dir = fe.create_directory ? (fs::path(rv.base) / fe.name).string() : rv.base;
            auto archive_path = (fs::path(ctx.cache_dir) / "archives" / rv.fname).string();
            fs::create_directories(fs::path(archive_path).parent_path());

            bool need_download = true;
            if (platform::file_exists(archive_path))
                if (fe.sha256.empty() || platform::file_hash(archive_path) == fe.sha256)
                    need_download = false;

            if (need_download)
            {
                ctx.logger->info("Downloading " + type + " " + fe.name + " from " + rv.url);
                auto prog = [&, label = std::string(fe.name)](size_t dl, size_t total)
                    { show_progress(label, dl, total); };
                if (!download::download_verify(rv.url, archive_path, fe.sha256, 2, prog))
                {
                    error = type + " download failed for " + fe.name;
                    return false;
                }
            }
            else
            {
                if (fe.sha256.empty())
                    ctx.logger->info("sha256 = \"" + platform::file_hash(archive_path)
                        + "\"  # add to config to verify " + archive_path);
                else
                    ctx.logger->info(type + " " + fe.name + " archive cached");
            }

            auto csum_path = (fs::path(extract_dir) / (fe.name + ".predepsum")).string();
            if (platform::file_exists(csum_path))
            {
                bool match = fe.sha256.empty();
                if (!fe.sha256.empty())
                {
                    std::ifstream f(csum_path);
                    std::string stored;
                    std::getline(f, stored);
                    match = (stored == fe.sha256);
                }
                if (match)
                {
                    ctx.logger->info(type + " " + fe.name + " extracted cached");
                    continue;
                }
            }

            fs::create_directories(extract_dir);
            bool ok = false;
            if (rv.fname.ends_with(".tar.gz") || rv.fname.ends_with(".tgz"))
                ok = extract::tar_gz(archive_path, extract_dir, fe.include, fe.exclude);
            else if (rv.fname.ends_with(".zip"))
                ok = extract::zip(archive_path, extract_dir, fe.include, fe.exclude);
            else
                ok = true;

            if (!ok)
            {
                error = "extraction failed for " + fe.name;
                return false;
            }

            if (fe.create_directory)
            {
                std::string root;
                int n = 0;
                for (auto &e : fs::directory_iterator(extract_dir))
                {
                    n++;
                    if (n == 1 && e.is_directory())
                        root = e.path().filename().string();
                    else if (n > 1)
                        root.clear();
                }
                if (n == 1 && !root.empty())
                {
                    auto src = (fs::path(extract_dir) / root).string();
                    for (auto &e : fs::directory_iterator(src))
                    {
                        auto name = e.path().filename().string();
                        auto dst = (fs::path(extract_dir) / name).string();
                        fs::remove_all(dst);
                        fs::rename(e.path(), dst);
                    }
                    fs::remove_all(src);
                }
            }

            // TODO: on sha256 mismatch, delete extract_dir before re-extracting
            {
                std::ofstream f(csum_path);
                if (!fe.sha256.empty())
                    f << fe.sha256 << "\n";
                else
                    f << platform::file_hash(archive_path) << "\n";
            }
        }
        else
        {
            auto dest_path = fe.create_directory
                ? (fs::path(rv.base) / fe.name / rv.fname).string()
                : (fs::path(rv.base) / rv.fname).string();

            auto parent = fs::path(dest_path).parent_path();
            fs::create_directories(parent);

            if (platform::file_exists(dest_path))
            {
                if (!fe.sha256.empty() && platform::file_hash(dest_path) != fe.sha256)
                {
                    ctx.logger->info(fe.name + " SHA256 mismatch, re-downloading");
                    fs::remove(dest_path);
                }
                else
                {
                    ctx.logger->info(type + " " + fe.name + " already up to date");
                    continue;
                }
            }

            ctx.logger->info("Downloading " + type + " " + fe.name + " from " + rv.url);

            auto prog = [&, label = std::string(fe.name)](size_t dl, size_t total)
                { show_progress(label, dl, total); };
            if (!download::download_verify(rv.url, dest_path, fe.sha256, 2, prog))
            {
                error = type + " download failed for " + fe.name;
                return false;
            }

            if (rv.output_name != fe.name)
            {
                auto dst = (fs::path(rv.base) / rv.output_name).string();
                if (!fs::exists(dst))
                    fs::rename(dest_path, dst);
            }
        }
    }
    return true;
}

bool download_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    auto *d = dynamic_cast<const download_data*>(sd.data.get());
    if (d && check_entries(*d, d->vars, ctx))
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

    if (d->entries.empty())
    {
        error = "stage " + sd.name + " has type '" + to_string(sd.type) + "' but no entries defined";
        return false;
    }

    auto type_label = d->fetch_type.empty() ? to_string(sd.type) : d->fetch_type;
    return resolve_entries(*d, d->vars, ctx, error, type_label);
}
