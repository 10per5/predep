#pragma once

#include "data/const.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class Logger;

struct artifact_entry
{
    std::string source;
    std::string dest;
};

struct vendor_entry
{
    std::string name;
    std::string url;
    std::string dest;
    std::string sha256;
    std::string output_name;
    bool extract = false;
    bool create_directory = false;
    std::map<std::string, std::string> vars;
};

struct platform_config
{
    std::vector<std::string> commands;
    std::string recipe;
    std::string target;
    std::string dest;
};

// --- polymorphic stage data (memory-efficient: each type carries only its own fields) ---

struct stage_data
{
    virtual ~stage_data() = default;
};

struct download_data : stage_data
{
    std::vector<vendor_entry> vendors;
    std::vector<vendor_entry> binaries;
    std::vector<vendor_entry> resources;
};

struct run_data : stage_data
{
    std::vector<std::string> commands;
};

struct docker_data : stage_data
{
    std::string recipe;
    std::string target;
    std::string dest;
};

struct package_data : stage_data
{
    std::vector<artifact_entry> artifacts;
    std::string bundle;
};

struct group_data : stage_data
{
};

// --- common stage descriptor ---

struct stage_desc
{
    std::string name;
    std::string description;
    std::vector<std::string> depends;
    std::vector<std::string> outputs;
    std::string type;
    std::string config_dir;

    std::map<std::string, platform_config> platforms;
    std::map<std::string, std::string> vars;

    std::unique_ptr<stage_data> data;
};

// --- runtime context ---

struct runtime
{
    std::string root;
    std::string cache_dir;
    std::string install_dir;
    std::string target_os;
    std::string platform;
    int max_concurrency = 1;
    Logger *logger = nullptr;

    std::string resolve_path(const std::string &path) const;
};

inline std::string runtime::resolve_path(const std::string &path) const
{
    auto result = path;
    auto p1 = result.find(path::cache);
    if (p1 != std::string::npos)
        result = result.substr(0, p1) + cache_dir + "/" + result.substr(p1 + path::cache.size());
    auto p2 = result.find(path::root);
    if (p2 != std::string::npos)
        result = result.substr(0, p2) + root + "/" + result.substr(p2 + path::root.size());
    return result;
}
