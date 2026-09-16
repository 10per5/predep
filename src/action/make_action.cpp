#include "action/make_action.h"
#include "cfg/config_loader.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/process.h"
#include "sys/platform.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {
std::map<std::string, std::string> build_vars(const runtime &ctx)
{
    std::map<std::string, std::string> v;
    v["PLATFORM"] = platform::to_string(ctx.platform);
    v["ARCH"] = platform::arch();
    auto cpu = platform::arch();
    if (cpu == "x86_64") cpu = "amd64";
    else if (cpu == "aarch64") cpu = "arm64";
    v["CPU"] = cpu;
    v["OS"] = ctx.target_os;
#ifdef _WIN32
    v["EXE_SUFFIX"] = ".exe";
#else
    v["EXE_SUFFIX"] = "";
#endif
    return v;
}

// Apply platform overrides onto a make entry (mirrors resolve()'s merge so the
// idempotency signature matches what actually gets built).
void merge_make_platform(make_entry &e, const make_data &d, platform_type pt)
{
    auto pit = d.platform.find(pt);
    if (pit == d.platform.end()) return;
    auto &p = pit->second;
    if (!p.source.empty())        e.source = p.source;
    e.targets.insert(e.targets.end(), p.targets.begin(), p.targets.end());
    for (auto &[k, v] : p.variables) e.variables[k] = v;
    if (!p.installPrefix.empty()) e.installPrefix = p.installPrefix;
    if (!p.prefixVar.empty())     e.prefixVar = p.prefixVar;
    if (p.install.has_value())    e.install = p.install;
    if (p.jobs != 0)              e.jobs = p.jobs;
}

std::string jobs_arg(int jobs)
{
    if (jobs > 0) return "-j" + std::to_string(jobs);
    if (jobs < 0) return "-j";
    auto n = std::thread::hardware_concurrency();
    return "-j" + std::to_string(n ? n : 1u);
}

std::string resolved_source(const make_entry &e, const std::string &bc,
                            const runtime &ctx)
{
    if (!e.source.empty())
        return config_loader::interpolate(ctx.resolve_path(e.source), build_vars(ctx));
    return action::resolve_cwd(bc, ctx);
}

std::string resolved_prefix(const make_entry &e, const runtime &ctx)
{
    if (e.installPrefix.empty())
        return {};
    return config_loader::interpolate(ctx.resolve_path(e.installPrefix), build_vars(ctx));
}

// Signature of everything that affects this stage's build output. Recorded in a
// per-source marker so a changed input forces a rebuild (same model as cmake).
std::string make_signature(const make_entry &e, const runtime &ctx,
                           const std::string &source)
{
    std::string ref;
    auto git_marker = (fs::path(source) / ".predepgit").string();
    if (platform::file_exists(git_marker))
    {
        std::ifstream f(git_marker);
        std::getline(f, ref);
    }

    std::string targets;
    for (auto &t : e.targets) targets += t + "\n";

    std::string vars;
    for (auto &[k, v] : e.variables) vars += k + "=" + v + "\n";

    std::string sig;
    sig += "source=" + source + "\n";
    sig += "targets=\n" + targets;
    sig += "variables=\n" + vars;
    sig += "prefix=" + resolved_prefix(e, ctx) + "\n";
    sig += "prefixVar=" + e.prefixVar + "\n";
    sig += "install=" + std::to_string(e.install.value_or(false)) + "\n";
    sig += "jobs=" + std::to_string(e.jobs) + "\n";
    sig += "ref=" + ref + "\n";
    return sig;
}

}

void make_action::parse(config_node &cfg, make_data &d)
{
    d.defaults.source = cfg.get_string("source");
    for (auto &t : cfg.get_array("targets"))
        d.defaults.targets.push_back(t.as_string());
    d.defaults.installPrefix = cfg.get_string("installPrefix");
    auto pv = cfg.get_string("prefixVar");
    if (!pv.empty()) d.defaults.prefixVar = pv;
    d.defaults.install = cfg.get_bool_flex("install", true);
    d.defaults.jobs = static_cast<int>(cfg.get_int("jobs", 0));

    auto vars = cfg.get_table("variables");
    if (vars)
        vars.for_each([&](const std::string &k, const config_node &v)
        {
            d.defaults.variables[k] = v.as_string();
        });

    auto plat = cfg.get_table("platform");
    if (!plat)
        return;

    plat.for_each([&](const std::string &key, const config_node &val)
    {
        auto pt = platform::from_string(key);
        platform_entry<make_entry> pe;
        auto src = val.get_string("source");
        if (!src.empty()) pe.source = src;
        for (auto &t : val.get_array("targets"))
            pe.targets.push_back(t.as_string());
        auto v = val.get_table("variables");
        if (v)
            v.for_each([&](const std::string &k, const config_node &n)
            {
                pe.variables[k] = n.as_string();
            });
        auto ip = val.get_string("installPrefix");
        if (!ip.empty()) pe.installPrefix = ip;
        auto pvar = val.get_string("prefixVar");
        if (!pvar.empty()) pe.prefixVar = pvar;
        if (val.has("install")) pe.install = val.get_bool_flex("install");
        if (val.has("jobs")) pe.jobs = static_cast<int>(val.get_int("jobs", 0));
        pe.build_context = val.get_string("build_context");
        d.platform[pt] = std::move(pe);
    });
}

bool make_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<make_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no make data";
        return false;
    }

    auto e = d->defaults;
    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();
    if (has_plat)
        merge_make_platform(e, *d, ctx.platform);

    if (!security::confirm_build_context(sd, d->build_context, "", ctx, error))
        return false;

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);

    auto source = resolved_source(e, bc, ctx);
    auto prefix = resolved_prefix(e, ctx);
    // make must run where the Makefile lives
    auto run_cwd = e.source.empty() ? cwd : source;

    std::vector<std::string> args;
    for (auto &[k, v] : e.variables)
    {
        auto key = config_loader::interpolate(k, build_vars(ctx));
        auto val = config_loader::interpolate(v, build_vars(ctx));
        args.push_back(key + "=" + val);
    }
    if (!prefix.empty())
        args.push_back(e.prefixVar + "=" + prefix);
    args.push_back(jobs_arg(e.jobs));
    for (auto &t : e.targets)
        args.push_back(config_loader::interpolate(t, build_vars(ctx)));

    ctx.logger->info("make " + sd.name);
    auto r1 = process::run_with_err("make", args, run_cwd);
    if (r1.code != 0)
    {
        if (!r1.err.empty()) ctx.logger->error(r1.err);
        error = "make failed for " + sd.name;
        return false;
    }

    if (e.install.value_or(false) && !prefix.empty())
    {
        ctx.logger->info("make install " + sd.name);
        std::vector<std::string> inst = {e.prefixVar + "=" + prefix, jobs_arg(e.jobs), "install"};
        auto r2 = process::run_with_err("make", inst, run_cwd);
        if (r2.code != 0)
        {
            if (!r2.err.empty()) ctx.logger->error(r2.err);
            error = "make install failed for " + sd.name;
            return false;
        }
    }

    auto marker = (fs::path(source) / ".predepmake").string();
    { std::ofstream f(marker); f << make_signature(e, ctx, source); }
    return true;
}

bool make_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    // If explicit outputs are declared, defer to the generic output check.
    auto *bd = dynamic_cast<const buildable_data*>(sd.data.get());
    if (bd && !bd->outputs.empty())
        return check_outputs(sd, ctx);

    auto *d = dynamic_cast<const make_data*>(sd.data.get());
    if (!d) return false;

    auto e = d->defaults;
    merge_make_platform(e, *d, ctx.platform);

    auto bc = d->build_context;
    auto pit = d->platform.find(ctx.platform);
    if (pit != d->platform.end() && !pit->second.build_context.empty())
        bc = pit->second.build_context;

    auto source = resolved_source(e, bc, ctx);
    auto marker = (fs::path(source) / ".predepmake").string();
    if (!platform::file_exists(marker))
        return false;
    auto sig = make_signature(e, ctx, source);
    std::ifstream f(marker);
    std::string stored((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return stored == sig;
}
