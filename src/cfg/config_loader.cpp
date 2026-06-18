#include "cfg/config_loader.h"
#include "action/download_action.h"
#include "action/run_action.h"
#include "action/docker_action.h"
#include "action/package_action.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

config_loader::config_loader(
    std::unordered_map<std::string, stage_desc> &stages,
    std::string &config_dir,
    std::string &error,
    std::string &main_stage)
    : m_stages(stages)
    , m_config_dir(config_dir)
    , m_error(error)
    , m_main_stage(main_stage)
{}

std::string config_loader::interpolate(
    const std::string &s,
    const std::map<std::string, std::string> &vars)
{
    // Build a lowercase-keyed lookup for case-insensitive matching
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

static void merge_root_array(
    std::unordered_map<std::string, stage_desc> &stages,
    const config_node &root,
    const std::string &key,
    std::vector<vendor_entry> download_data::*member,
    const std::string &default_dest)
{
    auto arr = root.get_array(key);
    for (auto &elem : arr)
    {
        auto ve = download_action::parse_entry(elem, default_dest);
        for (auto &[_, sd] : stages)
        {
            if (sd.type != key)
                continue;
            auto *dd = dynamic_cast<download_data*>(sd.data.get());
            if (!dd)
                continue;
            auto &vec = (*dd).*member;
            vec.push_back(ve);
        }
    }
}

static void parse_platform_overrides(const config_node &cfg, stage_desc &sd)
{
    auto plat = cfg.get_table("platform");
    if (!plat)
        return;
    plat.for_each([&](const std::string &key, const config_node &val)
    {
        platform_config pc;
        auto cmds = val.get_array("commands");
        for (auto &c : cmds)
            pc.commands.push_back(c.as_string());
        pc.recipe = val.get_string("recipe");
        pc.target = val.get_string("target");
        pc.dest = val.get_string("dest");
        pc.build_context = val.get_string("build_context");
        sd.platforms[key] = pc;
    });
}

static void parse_stage_vars(const config_node &cfg, stage_desc &sd)
{
    auto vars = cfg.get_table("vars");
    if (!vars)
        return;
    vars.for_each([&](const std::string &k, const config_node &v)
    {
        sd.vars[k] = v.as_string();
    });
}

static void parse_stages(
    std::unordered_map<std::string, stage_desc> &stages,
    const std::string &config_dir,
    const config_node &root,
    std::vector<std::string> *inserted = nullptr)
{
    auto is_download = [](const std::string &t) -> bool {
        return t == "download" || t == "vendor" || t == "binary" || t == "resource";
    };

    auto stages_arr = root.get_array("stages");

    for (auto &elem : stages_arr)
    {
        stage_desc sd;
        sd.name = elem.get_string("name");
        sd.description = elem.get_string("description");
        sd.type = elem.get_string("type", "run");

        auto deps = elem.get_array("depends");
        for (auto &dep : deps)
            sd.depends.push_back(dep.as_string());

        auto outs = elem.get_array("outputs");
        for (auto &out : outs)
            sd.outputs.push_back(out.as_string());

        sd.build_context = elem.get_string("build_context");

        if (is_download(sd.type))
        {
            auto d = std::make_unique<download_data>();
            download_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else if (sd.type == "run")
        {
            auto d = std::make_unique<run_data>();
            run_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else if (sd.type == "docker")
        {
            auto d = std::make_unique<docker_data>();
            docker_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else if (sd.type == "package")
        {
            auto d = std::make_unique<package_data>();
            package_action::parse(elem, *d);
            sd.data = std::move(d);
        }
        else
        {
            sd.data = std::make_unique<group_data>();
        }

        if (sd.type == "run" || sd.type == "docker")
            parse_platform_overrides(elem, sd);

        parse_stage_vars(elem, sd);

        sd.config_dir = config_dir;
        auto stage_name = sd.name;
        stages[stage_name] = std::move(sd);
        if (inserted)
            inserted->push_back(stage_name);
    }

    merge_root_array(stages, root, "vendor", &download_data::vendors, "root://vendor/");
    merge_root_array(stages, root, "binary", &download_data::binaries, "cache://bin/");
    merge_root_array(stages, root, "resource", &download_data::resources, "root://resources/");
}

static bool add_single_toml(
    std::unordered_map<std::string, stage_desc> &stages,
    std::string &config_dir,
    std::string &error,
    const std::string &path,
    const std::string &cfg_dir,
    const std::string &prefix,
    const std::vector<std::string> &only)
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
        parse_stages(stages, config_dir, root, &stage_names);

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
    auto root = config_node::parse_file(path);

    if (!add_single_toml(m_stages, m_config_dir, m_error, path, m_config_dir, "", {}))
        return false;

    m_main_stage = root.get_string("main");

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
        if (!add_single_toml(m_stages, m_config_dir, m_error, full, inc_dir, ns, only))
            return false;
    }

    return true;
}
