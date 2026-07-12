#include "cfg/config_loader.h"
#include "action/download_action.h"
#include "action/run_action.h"
#include "action/binary_action.h"
#include "action/premake5_action.h"
#include "action/docker_action.h"
#include "action/package_action.h"
#include "action/install_action.h"
#include "action/uninstall_action.h"
#include "action/clean_action.h"
#include "action/copy_action.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

config_loader::config_loader(
    std::unordered_map<std::string, stage_desc> &stages,
    std::string &config_dir,
    std::string &error,
    std::string &main_stage,
    std::string &project,
    std::vector<std::string> &config_files)
    : m_stages(stages)
    , m_config_dir(config_dir)
    , m_error(error)
    , m_main_stage(main_stage)
    , m_project(project)
    , m_config_files(config_files)
{}

std::string config_loader::interpolate(
    const std::string &s,
    const std::map<std::string, std::string> &vars)
{
    std::map<std::string, std::string> lower;
    for (auto &[k, v] : vars)
    {
        auto lk = k;
        for (auto &c : lk) c = static_cast<char>(std::tolower(c));
        lower[lk] = v;
    }

    auto result = s;
    auto lo = result;
    for (auto &c : lo) c = static_cast<char>(std::tolower(c));

    for (auto &[key, val] : lower)
    {
        auto pat = "${" + key + "}";
        for (auto pos = lo.find(pat); pos != std::string::npos;
             pos = lo.find(pat, pos + val.size()))
        {
            result.replace(pos, pat.size(), val);
            lo.replace(pos, pat.size(), val);
            for (auto i = pos; i < pos + val.size(); i++)
                lo[i] = static_cast<char>(std::tolower(lo[i]));
            pos += val.size();
        }
    }
    return result;
}

static void merge_root_entries(
    std::unordered_map<std::string, stage_desc> &stages,
    const config_node &root,
    stage_type stype,
    const std::string &root_array_name,
    const std::string &implicit_fetch_type,
    const std::string &default_dest,
    const std::string &fetch_type_filter = "")
{
    auto arr = root.get_array(root_array_name);
    for (auto &elem : arr)
    {
        auto fe = download_action::parse_entry(elem, default_dest);
        if (fe.fetch_type.empty())
            fe.fetch_type = implicit_fetch_type;
        for (auto &[_, sd] : stages)
        {
            if (sd.type != stype)
            {
                if (!fetch_type_filter.empty() && sd.type == stage_type::fetch)
                {
                    auto *dd = dynamic_cast<download_data*>(sd.data.get());
                    if (!dd || dd->fetch_type != fetch_type_filter)
                        continue;
                }
                else
                    continue;
            }
            else if (!fetch_type_filter.empty() && stype == stage_type::fetch)
            {
                auto *dd = dynamic_cast<download_data*>(sd.data.get());
                if (!dd || dd->fetch_type != fetch_type_filter)
                    continue;
            }
            auto *dd = dynamic_cast<download_data*>(sd.data.get());
            if (!dd)
                continue;
            if (!dd->assets.empty()
                && std::find(dd->assets.begin(), dd->assets.end(), fe.name) == dd->assets.end())
                continue;
            dd->entries.push_back(fe);
        }
    }
}

static void parse_stages(
    std::unordered_map<std::string, stage_desc> &stages,
    const std::string &config_dir,
    const std::string &source_file,
    const config_node &root,
    std::vector<std::string> *inserted = nullptr,
    const std::string &engine_project = "")
{
    auto is_download = [](stage_type t) -> bool {
        return t == stage_type::vendor || t == stage_type::fetch || t == stage_type::resource;
    };

    auto stages_arr = root.get_array("stages");

    for (auto &elem : stages_arr)
    {
        stage_desc sd;
        sd.name = elem.get_string("name");
        sd.description = elem.get_string("description");
        sd.type = stage_from_string(elem.get_string("type", "disabled"));

        auto deps = elem.get_array("depends");
        for (auto &dep : deps)
            sd.depends.push_back(dep.as_string());

        if (is_download(sd.type))
        {
            auto d = std::make_unique<download_data>();
            download_action::parse(elem, *d);

            if (sd.type == stage_type::fetch && d->fetch_type.empty())
                throw std::runtime_error("stage '" + sd.name + "' has type 'fetch' but no 'fetch-type' field");

            auto vars = elem.get_table("vars");
            if (vars)
            {
                vars.for_each([&](const std::string &k, const config_node &v)
                {
                    d->vars[k] = v.as_string();
                });
            }

            d->clean = elem.get_bool_flex("clean");
            auto clean_paths = elem.get_array("clean_paths");
            for (auto &cp : clean_paths)
                d->clean_paths.push_back(cp.as_string());

            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::run)
        {
            auto d = std::make_unique<run_data>();
            run_action::parse(elem, *d);

            auto outs = elem.get_array("outputs");
            for (auto &out : outs)
                d->outputs.push_back(out.as_string());
            d->build_context = elem.get_string("build_context");
            d->clean = elem.get_bool_flex("clean");
            auto clean_paths = elem.get_array("clean_paths");
            for (auto &cp : clean_paths)
                d->clean_paths.push_back(cp.as_string());

            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::premake5)
        {
            auto d = std::make_unique<premake5_data>();
            premake5_action::parse(elem, *d);

            if (d->defaults.project.empty())
                d->defaults.project = engine_project;

            auto outs = elem.get_array("outputs");
            for (auto &out : outs)
                d->outputs.push_back(out.as_string());
            d->build_context = elem.get_string("build_context");
            d->clean = elem.get_bool_flex("clean");
            auto clean_paths = elem.get_array("clean_paths");
            for (auto &cp : clean_paths)
                d->clean_paths.push_back(cp.as_string());

            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::docker)
        {
            auto d = std::make_unique<docker_data>();
            docker_action::parse(elem, *d);

            auto outs = elem.get_array("outputs");
            for (auto &out : outs)
                d->outputs.push_back(out.as_string());
            d->build_context = elem.get_string("build_context");
            d->clean = elem.get_bool_flex("clean");
            auto clean_paths = elem.get_array("clean_paths");
            for (auto &cp : clean_paths)
                d->clean_paths.push_back(cp.as_string());

            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::package)
        {
            auto d = std::make_unique<package_data>();
            package_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::binary)
        {
            auto d = std::make_unique<binary_data>();
            binary_action::parse(elem, *d);

            // Root-level [params.<type>.<name>] supplies structured params
            auto params_root = root.get_table("params");
            if (params_root)
            {
                auto type_params = params_root.get_table("binary");
                if (type_params)
                {
                    auto stage_params = type_params.get_table(sd.name);
                    if (stage_params)
                    {
                        stage_params.for_each([&](const std::string &k, const config_node &v)
                        {
                            d->defaults.params[k] = v.as_string();
                        });
                    }
                }
            }

            auto outs = elem.get_array("outputs");
            for (auto &out : outs)
                d->outputs.push_back(out.as_string());
            d->build_context = elem.get_string("build_context");
            d->clean = elem.get_bool_flex("clean");
            auto clean_paths = elem.get_array("clean_paths");
            for (auto &cp : clean_paths)
                d->clean_paths.push_back(cp.as_string());

            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::clean)
        {
            auto d = std::make_unique<clean_data>();
            clean_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::copy)
        {
            auto d = std::make_unique<copy_data>();
            copy_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else if (sd.type == stage_type::disabled)
        {
            sd.data = std::make_unique<group_data>();
        }
        else
        {
            sd.data = std::make_unique<group_data>();
        }

        sd.config_dir = config_dir;
        sd.source_file = source_file;
        auto stage_name = sd.name;
        stages[stage_name] = std::move(sd);
        if (inserted)
            inserted->push_back(stage_name);
    }

    auto merge_fetch = [&]()
    {
        auto arr = root.get_array("fetch");
        for (auto &elem : arr)
        {
            std::string default_dest;
            auto ft = elem.get_string("fetch-type");
            if (ft.empty())
                continue;
            if (ft == "binary")       default_dest = "cache://bin/";
            else if (ft == "vendor")  default_dest = "root://vendor/";
            else if (ft == "resource") default_dest = "root://resources/";
            else continue;

            auto fe = download_action::parse_entry(elem, default_dest);
            if (fe.fetch_type.empty())
                fe.fetch_type = ft;

            for (auto &[_, sd] : stages)
            {
                if (sd.type != stage_type::fetch)
                    continue;
                auto *dd = dynamic_cast<download_data*>(sd.data.get());
                if (!dd || dd->fetch_type != ft)
                    continue;
                if (!dd->assets.empty()
                    && std::find(dd->assets.begin(), dd->assets.end(), fe.name) == dd->assets.end())
                    continue;
                dd->entries.push_back(fe);
            }
        }
    };
    merge_fetch();

    merge_root_entries(stages, root, stage_type::vendor, "vendor", "", "root://vendor/", "vendor");
    merge_root_entries(stages, root, stage_type::fetch, "binary", "binary", "cache://bin/", "binary");
    merge_root_entries(stages, root, stage_type::resource, "resource", "", "root://resources/", "resource");

    // Merge scoped root-level [platform.<os>] overrides
    auto root_plat = root.get_table("platform");
    if (root_plat)
    {
        root_plat.for_each([&](const std::string &os_key, const config_node &os_val)
        {
            auto pt = platform::from_string(os_key);
            if (pt == platform_type::linux && os_key != "linux")
                return;

            // --- Stage overrides: platform.<os>.stage.<name> ---
            auto stage_tbl = os_val.get_table("stage");
            if (stage_tbl)
            {
                stage_tbl.for_each([&](const std::string &sname, const config_node &sv)
                {
                    auto it = stages.find(sname);
                    if (it == stages.end())
                        return;
                    auto &sd = it->second;

                    switch (sd.type)
                    {
                        case stage_type::docker:
                        {
                            auto *d = dynamic_cast<docker_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<docker_entry> pe;
                            pe.recipe = sv.get_string("recipe");
                            pe.target = sv.get_string("target");
                            pe.dest = sv.get_string("dest");
                            pe.build_context = sv.get_string("build_context");
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        case stage_type::run:
                        {
                            auto *d = dynamic_cast<run_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<run_entry> pe;
                            auto cmds = sv.get_array("commands");
                            for (auto &c : cmds)
                                pe.commands.push_back(c.as_string());
                            pe.build_context = sv.get_string("build_context");
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        case stage_type::binary:
                        {
                            auto *d = dynamic_cast<binary_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<binary_entry> pe;
                            auto pparams = sv.get_table("params");
                            if (pparams)
                            {
                                pparams.for_each([&](const std::string &pk, const config_node &pv)
                                {
                                    pe.params[pk] = pv.as_string();
                                });
                            }
                            auto pargs = sv.get_array("args");
                            for (auto &a : pargs)
                                pe.args.push_back(a.as_string());
                            pe.build_context = sv.get_string("build_context");
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        case stage_type::premake5:
                        {
                            auto *d = dynamic_cast<premake5_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<premake5_entry> pe;
                            auto act = sv.get_string("action");
                            if (!act.empty()) pe.action = act;
                            if (sv.has("make")) pe.make = sv.get_bool_flex("make");
                            if (sv.has("strip")) pe.strip = sv.get_bool_flex("strip");
                            auto tgt = sv.get_string("target");
                            if (!tgt.empty()) pe.target = tgt;
                            auto proj = sv.get_string("project");
                            if (!proj.empty()) pe.project = proj;
                            auto cfg_val = sv.get_string("config");
                            if (!cfg_val.empty()) pe.config = cfg_val;
                            pe.build_context = sv.get_string("build_context");
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        case stage_type::install:
                        {
                            auto *d = dynamic_cast<install_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<install_entry> pe;
                            auto dir = sv.get_string("dir");
                            if (!dir.empty()) pe.dir = dir;
                            auto arts = sv.get_array("artifacts");
                            for (auto &elem : arts)
                            {
                                artifact_entry ae;
                                ae.source = elem.get_string("source");
                                ae.dest = elem.get_string("dest");
                                ae.userdir = elem.get_bool_flex("userdir");
                                pe.artifacts.push_back(ae);
                            }
                            if (sv.has("symlink")) pe.symlink = sv.get_bool_flex("symlink");
                            pe.build_context = sv.get_string("build_context");
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        case stage_type::uninstall:
                        {
                            auto *d = dynamic_cast<uninstall_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<install_entry> pe;
                            auto dir = sv.get_string("dir");
                            if (!dir.empty()) pe.dir = dir;
                            auto arts = sv.get_array("artifacts");
                            for (auto &elem : arts)
                            {
                                artifact_entry ae;
                                ae.source = elem.get_string("source");
                                ae.dest = elem.get_string("dest");
                                ae.userdir = elem.get_bool_flex("userdir");
                                pe.artifacts.push_back(ae);
                            }
                            if (sv.has("symlink")) pe.symlink = sv.get_bool_flex("symlink");
                            pe.build_context = sv.get_string("build_context");
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        case stage_type::copy:
                        {
                            auto *d = dynamic_cast<copy_data*>(sd.data.get());
                            if (!d) break;
                            platform_entry<copy_entry> pe;
                            auto sd_dir = sv.get_string("source_dir");
                            if (!sd_dir.empty()) pe.source_dir = sd_dir;
                            if (sv.has("fail_if_missing"))
                                pe.fail_if_missing = sv.get_bool_flex("fail_if_missing");
                            auto farr = sv.get_array("files");
                            for (auto &elem : farr)
                            {
                                copy_file cf;
                                cf.source = elem.get_string("source");
                                auto dests = elem.get_array("dests");
                                for (auto &dd : dests)
                                    cf.dests.push_back(dd.as_string());
                                pe.files.push_back(std::move(cf));
                            }
                            d->platform[pt] = std::move(pe);
                            break;
                        }
                        default:
                            break;
                    }
                });
            }

            // --- Entry overrides: platform.<os>.binary/vendor/resource.<name> ---
            auto override_entries = [&](const std::string &scope)
            {
                auto tbl = os_val.get_table(scope);
                if (!tbl) return;
                tbl.for_each([&](const std::string &entry_name, const config_node &ev)
                {
                    for (auto &[_, sd] : stages)
                    {
                        if (!is_download(sd.type))
                            continue;
                        auto *dd = dynamic_cast<download_data*>(sd.data.get());
                        if (!dd) continue;
                        for (auto &fe : dd->entries)
                        {
                            if (fe.name != entry_name)
                                continue;

                            platform_entry<fetch_entry> pe;
                            auto url = ev.get_string("url");
                            if (!url.empty()) pe.url = url;
                            auto sha256 = ev.get_string("sha256");
                            if (!sha256.empty()) pe.sha256 = sha256;
                            auto dest = ev.get_string("dest");
                            if (!dest.empty()) pe.dest = dest;
                            auto output_name = ev.get_string("output_name");
                            if (!output_name.empty()) pe.output_name = output_name;
                            if (ev.has("extract"))
                                pe.extract = ev.get_bool_flex("extract");
                            if (ev.has("create_directory"))
                                pe.create_directory = ev.get_bool_flex("create_directory");
                            // Note: extract/create_directory bool overrides only
                            // work for true→false direction; false→true misses
                            // due to plain bool default in platform_entry.
                            auto ver = ev.get_string("version");
                            if (!ver.empty()) pe.vars["version"] = ver;

                            auto vars_tbl = ev.get_table("variables");
                            if (vars_tbl)
                            {
                                vars_tbl.for_each([&](const std::string &vk, const config_node &vv)
                                {
                                    if (vv.is_string())
                                        pe.vars[vk] = vv.as_string();
                                });
                            }

                            fe.platform[pt] = std::move(pe);
                        }
                    }
                });
            };
            override_entries("binary");
            override_entries("vendor");
            override_entries("resource");
        });
    }
}

static bool add_single_toml(
    std::unordered_map<std::string, stage_desc> &stages,
    std::string &config_dir,
    std::string &error,
    const std::string &path,
    const std::string &cfg_dir,
    const std::string &prefix,
    const std::vector<std::string> &only,
    const std::string &engine_project = "")
{
    try
    {
        if (!fs::exists(path))
        {
            error = "included manifest not found: " + path;
            return false;
        }

        auto saved = config_dir;
        config_dir = cfg_dir;

        std::vector<std::string> stage_names;
        auto root = config_node::parse_file(path);

        // Use a temp map so include stages never overwrite parent stages
        // with bare names before prefixing
        std::unordered_map<std::string, stage_desc> tmp;
        parse_stages(tmp, config_dir, path, root, &stage_names, engine_project);

        if (!prefix.empty() || !only.empty())
        {
            std::set<std::string> orig_names(stage_names.begin(), stage_names.end());

            for (auto &name : stage_names)
            {
                auto it = tmp.find(name);
                if (it == tmp.end())
                    continue;

                if (!only.empty() && std::find(only.begin(), only.end(), name) == only.end())
                {
                    tmp.erase(name);
                    continue;
                }

                if (!prefix.empty())
                {
                    auto prefixed = prefix + "::" + name;
                    stages[prefixed] = std::move(it->second);
                    tmp.erase(name);

                    for (auto &dep : stages[prefixed].depends)
                    {
                        if (orig_names.count(dep))
                            dep = prefix + "::" + dep;
                    }
                }
            }
        }

        // Move remaining unprefixed stages into the parent map
        for (auto &[n, sd] : tmp)
            stages[n] = std::move(sd);

        config_dir = saved;
        return true;
    }
    catch (const std::exception &e)
    {
        error = std::string(e.what()) + " (in " + path + ")";
        return false;
    }
}

bool config_loader::load(const std::string &path)
{
    m_config_files.clear();

    auto root = config_node::parse_file(path);

    m_main_stage = root.get_string("main");
    m_project = root.get_string("project");

    m_config_files.push_back(path);
    if (!add_single_toml(m_stages, m_config_dir, m_error, path, m_config_dir, "", {}, m_project))
        return false;

    // Auto-generate install/uninstall stages from [install] root config
    auto install_cfg = root.get_table("install");
    if (install_cfg)
    {
        auto install_dir = install_cfg.get_string("dir");
        auto symlink = install_cfg.get_bool_flex("symlink", false);

        std::vector<artifact_entry> artifacts;
        auto art_arr = install_cfg.get_array("artifacts");
        for (auto &elem : art_arr)
        {
            artifact_entry ae;
            ae.source = elem.get_string("source");
            ae.dest = elem.get_string("dest");
            artifacts.push_back(ae);
        }

        if (artifacts.empty())
        {
            m_error = "[install] table requires at least one artifact entry";
            return false;
        }

        // Only auto-generate if no manual "install" stage already exists
        if (m_stages.find("install") == m_stages.end())
        {
            stage_desc install_sd;
            install_sd.name = "install";
            install_sd.type = stage_type::install;

            auto deps = install_cfg.get_array("depends");
            for (auto &dep : deps)
                install_sd.depends.push_back(dep.as_string());

            auto id = std::make_unique<install_data>();
            id->defaults.dir = install_dir;
            id->defaults.artifacts = artifacts;
            id->defaults.symlink = symlink;
            install_sd.data = std::move(id);
            install_sd.config_dir = m_config_dir;
            install_sd.source_file = path;

            m_stages["install"] = std::move(install_sd);
        }

        // Only auto-generate if no manual "uninstall" stage already exists
        if (m_stages.find("uninstall") == m_stages.end())
        {
            stage_desc uninstall_sd;
            uninstall_sd.name = "uninstall";
            uninstall_sd.type = stage_type::uninstall;

            auto ud = std::make_unique<uninstall_data>();
            ud->defaults.dir = install_dir;
            ud->defaults.artifacts = artifacts;
            ud->defaults.symlink = symlink;
            uninstall_sd.data = std::move(ud);
            uninstall_sd.config_dir = m_config_dir;
            uninstall_sd.source_file = path;

            m_stages["uninstall"] = std::move(uninstall_sd);
        }
    }

    auto includes = root.get_array("include");
    for (auto &elem : includes)
    {
        auto rel = elem.get_string("path");
        if (rel.empty())
        {
            m_error = "include entry missing 'path'";
            return false;
        }

        auto ns = elem.get_string("namespace");
        std::vector<std::string> only;

        auto only_arr = elem.get_array("only");
        for (auto &v : only_arr)
            only.push_back(v.as_string());

        if (ns.empty())
        {
            auto dir = fs::path(rel).parent_path().string();
            if (!dir.empty())
                ns = dir;
        }

        auto full = (fs::path(m_config_dir) / rel).string();
        auto inc_dir = fs::absolute(fs::path(full).parent_path()).string();
        m_config_files.push_back(full);
        if (!add_single_toml(m_stages, m_config_dir, m_error, full, inc_dir, ns, only, m_project))
            return false;
    }

    return true;
}
