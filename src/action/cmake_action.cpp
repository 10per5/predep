#include "action/cmake_action.h"
#include "cfg/config_loader.h"
#include "logger/logger.h"
#include "security/security.h"
#include "sys/process.h"
#include "sys/platform.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

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
}

void cmake_action::parse(config_node &cfg, cmake_data &d)
{
    d.defaults.source         = cfg.get_string("source");
    d.defaults.build_dir      = cfg.get_string("build_dir");
    d.defaults.config         = cfg.get_string("config", "Release");
    d.defaults.installPrefix  = cfg.get_string("installPrefix");
    d.defaults.install        = cfg.get_bool_flex("install", true);

    auto add = [&](const config_node &n, std::vector<std::string> &out, const std::string &key)
    {
        for (auto &a : n.get_array(key))
            out.push_back(a.as_string());
    };
    add(cfg, d.defaults.flagsOn, "flagsOn");
    add(cfg, d.defaults.flagsOff, "flagsOff");
    add(cfg, d.defaults.configurable, "configurable");
    add(cfg, d.defaults.installPrefixVars, "installPrefixVars");
    add(cfg, d.defaults.targets, "targets");

    auto plat = cfg.get_table("platform");
    if (plat)
    {
        plat.for_each([&](const std::string &key, const config_node &val)
        {
            auto pt = platform::from_string(key);
            platform_entry<cmake_entry> pe;
            auto src = val.get_string("source");          if (!src.empty()) pe.source = src;
            auto bd = val.get_string("build_dir");        if (!bd.empty()) pe.build_dir = bd;
            auto cf = val.get_string("config");           if (!cf.empty()) pe.config = cf;
            auto ip = val.get_string("installPrefix");    if (!ip.empty()) pe.installPrefix = ip;
            if (val.has("install")) pe.install = val.get_bool_flex("install");
            add(val, pe.flagsOn, "flagsOn");
            add(val, pe.flagsOff, "flagsOff");
            add(val, pe.configurable, "configurable");
            add(val, pe.installPrefixVars, "installPrefixVars");
            add(val, pe.targets, "targets");
            pe.build_context = val.get_string("build_context");
            d.platform[pt] = std::move(pe);
        });
    }
}

bool cmake_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    auto *d = dynamic_cast<cmake_data*>(sd.data.get());
    if (!d)
    {
        error = "stage " + sd.name + " has no cmake data";
        return false;
    }

    auto e = d->defaults;
    auto pit = d->platform.find(ctx.platform);
    bool has_plat = pit != d->platform.end();
    if (has_plat)
    {
        auto &p = pit->second;
        if (!p.source.empty())        e.source = p.source;
        if (!p.build_dir.empty())     e.build_dir = p.build_dir;
        if (!p.config.empty())        e.config = p.config;
        if (!p.installPrefix.empty()) e.installPrefix = p.installPrefix;
        e.flagsOn.insert(e.flagsOn.end(), p.flagsOn.begin(), p.flagsOn.end());
        e.flagsOff.insert(e.flagsOff.end(), p.flagsOff.begin(), p.flagsOff.end());
        e.configurable.insert(e.configurable.end(), p.configurable.begin(), p.configurable.end());
        e.installPrefixVars.insert(e.installPrefixVars.end(), p.installPrefixVars.begin(), p.installPrefixVars.end());
        e.targets.insert(e.targets.end(), p.targets.begin(), p.targets.end());
    }

    if (!security::confirm_build_context(sd, d->build_context, "", ctx, error))
        return false;

    auto bc = d->build_context;
    if (has_plat && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    auto cwd = action::resolve_cwd(bc, ctx);

    auto vars = build_vars(ctx);
    auto source = config_loader::interpolate(ctx.resolve_path(e.source), vars);
    auto build_dir = !e.build_dir.empty()
        ? config_loader::interpolate(ctx.resolve_path(e.build_dir), vars)
        : (fs::path(source) / "build").string();
    auto prefix = !e.installPrefix.empty()
        ? config_loader::interpolate(ctx.resolve_path(e.installPrefix), vars)
        : (fs::path(source) / "prefix").string();
    for (auto &c : e.configurable)
        c = config_loader::interpolate(c, vars);

    std::vector<std::string> cfg_args = {"-S", source, "-B", build_dir};
    cfg_args.push_back("-DCMAKE_BUILD_TYPE=" + e.config);
    cfg_args.push_back("-DCMAKE_INSTALL_PREFIX=" + prefix);
    for (auto &f : e.flagsOn)  cfg_args.push_back("-D" + f + "=ON");
    for (auto &f : e.flagsOff) cfg_args.push_back("-D" + f + "=OFF");
    for (auto &c : e.configurable) cfg_args.push_back("-D" + c);
    for (auto &v : e.installPrefixVars) cfg_args.push_back("-D" + v + "=" + prefix);

    ctx.logger->info("cmake configure " + sd.name);
    auto r1 = process::run_with_err("cmake", cfg_args, cwd);
    if (r1.code != 0)
    {
        if (!r1.err.empty()) ctx.logger->error(r1.err);
        error = "cmake configure failed for " + sd.name;
        return false;
    }

    std::vector<std::string> build_args = {"--build", build_dir};
    if (!e.targets.empty())
    {
        build_args.push_back("--target");
        for (auto &t : e.targets) build_args.push_back(t);
    }
    else
    {
        build_args.push_back("-j");
    }
    ctx.logger->info("cmake build " + sd.name);
    auto r2 = process::run_with_err("cmake", build_args, cwd);
    if (r2.code != 0)
    {
        if (!r2.err.empty()) ctx.logger->error(r2.err);
        error = "cmake build failed for " + sd.name;
        return false;
    }

    if (e.install)
    {
        ctx.logger->info("cmake install " + sd.name);
        auto r3 = process::run_with_err("cmake", {"--install", build_dir}, cwd);
        if (r3.code != 0)
        {
            if (!r3.err.empty()) ctx.logger->error(r3.err);
            error = "cmake install failed for " + sd.name;
            return false;
        }
    }
    return true;
}

bool cmake_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    // If explicit outputs are declared, defer to the generic output check.
    auto *bd = dynamic_cast<const buildable_data*>(sd.data.get());
    if (bd && !bd->outputs.empty())
        return check_outputs(sd, ctx);

    auto *d = dynamic_cast<const cmake_data*>(sd.data.get());
    if (!d) return false;

    auto e = d->defaults;
    auto pit = d->platform.find(ctx.platform);
    if (pit != d->platform.end())
    {
        auto &p = pit->second;
        if (!p.installPrefix.empty()) e.installPrefix = p.installPrefix;
        if (!p.source.empty())       e.source = p.source;
    }

    auto prefix = !e.installPrefix.empty()
        ? ctx.resolve_path(e.installPrefix)
        : (ctx.resolve_path(e.source) + "/prefix");
    if (e.install)
    {
        if (!platform::dir_exists(prefix))
            return false;
        bool any = false;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(prefix, ec);
             it != fs::recursive_directory_iterator(); ++it)
        {
            any = true;
            break;
        }
        return any;
    }

    auto build_dir = !e.build_dir.empty()
        ? ctx.resolve_path(e.build_dir)
        : (ctx.resolve_path(e.source) + "/build");
    return platform::dir_exists(build_dir);
}
