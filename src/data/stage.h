#pragma once

#include "data/const.h"
#include "sys/platform.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Logger;
class Prompter;

// ---- Stage type enum ----

enum class stage_type { vendor, fetch, resource, run, docker, premake5, package, group, disabled, binary, install, uninstall };

stage_type stage_from_string(const std::string &);
std::string to_string(stage_type);

// ---- Entry structs (pure data, shared for defaults and platform overrides) ----

struct artifact_entry
{
    std::string source;
    std::string dest;
    bool userdir = false;
};

struct fetch_entry
{
    std::string fetch_type;
    std::string name;
    std::string url;
    std::string dest;
    std::string sha256;
    std::string output_name;
    bool extract = false;
    bool create_directory = false;
    std::map<std::string, std::string> vars;
};

struct run_entry
{
    std::vector<std::string> commands;
};

struct docker_entry
{
    std::string recipe;
    std::string target;
    std::string dest;
};

struct premake5_entry
{
    std::string action = "gmake";
    std::optional<bool> make = true;
    std::optional<bool> strip = true;
    std::string target;
    std::string project;
};

// ---- Platform entry template (adds build_context override to any entry type) ----

template<typename T>
struct platform_entry : T
{
    std::string build_context;   // empty = not set → use buildable_data default
};

// ---- Polymorphic dispatch base ----

struct stage_data
{
    virtual ~stage_data() = default;
};

struct download_data : stage_data
{
    std::string fetch_type;
    std::vector<std::string> assets;
    std::vector<fetch_entry> entries;
    std::map<std::string, std::string> vars;
};

struct buildable_data : stage_data
{
    std::vector<std::string> outputs;
    std::string build_context;
};

struct run_data : buildable_data
{
    run_entry defaults;
    std::map<platform_type, platform_entry<run_entry>> platform;
};

struct docker_data : buildable_data
{
    docker_entry defaults;
    std::map<platform_type, platform_entry<docker_entry>> platform;
};

struct premake5_data : buildable_data
{
    premake5_entry defaults;
    std::map<platform_type, platform_entry<premake5_entry>> platform;
};

struct package_data : stage_data
{
    std::vector<artifact_entry> artifacts;
    std::string bundle;
};

// Roadmap: external binary metadata files (e.g. binaries/*.toml) will define
// known binaries with per-argument safety levels (safe/warning/dangerous),
// argument schemas, flag prefix conventions, and validation rules.
// Until then, all binary params/args are treated as DANGEROUS.

struct binary_entry
{
    // --key value pairs (structured, but still DANGEROUS — no schema yet)
    std::map<std::string, std::string> params;
    // Free-form argv tokens (DANGEROUS — no structure at all)
    std::vector<std::string> args;
};

struct binary_data : buildable_data
{
    std::string binary_name;
    binary_entry defaults;
    std::map<platform_type, platform_entry<binary_entry>> platform;
};

struct install_entry
{
    std::string dir;                              // install prefix (default: /usr/local/bin)
    std::vector<artifact_entry> artifacts;         // source → dest (dest relative to dir)
    bool symlink = false;                          // create /usr/local/bin/<project> symlink
};

struct install_data : buildable_data
{
    install_entry defaults;
    std::map<platform_type, platform_entry<install_entry>> platform;
};

struct uninstall_data : buildable_data
{
    install_entry defaults;
    std::map<platform_type, platform_entry<install_entry>> platform;
};

struct group_data : stage_data
{
};

// ---- Stage descriptor ----

struct stage_desc
{
    std::string name;
    std::string description;
    stage_type type;
    std::vector<std::string> depends;
    std::string config_dir;
    std::string source_file;
    std::unique_ptr<stage_data> data;
};

// ---- Runtime context ----

struct runtime
{
    std::string root;
    std::string cache_dir;
    std::string target_os;
    std::string project;
    platform_type platform;
    int max_concurrency = 1;
    bool privileged = false;
    std::string config_sha;
    Logger *logger = nullptr;
    Prompter *prompter = nullptr;

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
