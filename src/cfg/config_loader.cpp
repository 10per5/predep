#include "cfg/config_loader.h"
#include "action/download_action.h"
#include "action/run_action.h"
#include "action/binary_action.h"
#include "action/premake5_action.h"
#include "action/docker_action.h"
#include "action/package_action.h"
#include "action/install_action.h"
#include "action/uninstall_action.h"
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

            auto outs = elem.get_array("outputs");
            for (auto &out : outs)
                d->outputs.push_back(out.as_string());
            d->build_context = elem.get_string("build_context");

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
        parse_stages(stages, config_dir, path, root, &stage_names, engine_project);

        if (!prefix.empty() || !only.empty())
        {
            std::set<std::string> orig_names(stage_names.begin(), stage_names.end());

            for (auto &name : stage_names)
            {
                if (!only.empty() && std::find(only.begin(), only.end(), name) == only.end())
                {
                    stages.erase(name);
                    continue;
                }

                if (!prefix.empty())
                {
                    auto prefixed = prefix + "::" + name;
                    stages[prefixed] = std::move(stages[name]);
                    stages.erase(name);

                    for (auto &dep : stages[prefixed].depends)
                    {
                        if (orig_names.count(dep))
                            dep = prefix + "::" + dep;
                    }
                }
            }
        }

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
        auto symlink = install_cfg.get_bool("symlink", false);

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

        auto full = m_config_dir + "/" + rel;
        auto inc_dir = fs::absolute(fs::path(full).parent_path()).string();
        m_config_files.push_back(full);
        if (!add_single_toml(m_stages, m_config_dir, m_error, full, inc_dir, ns, only, m_project))
            return false;
    }

    return true;
}
