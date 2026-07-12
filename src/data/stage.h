#pragma once

#include "data/const.h"
#include "sys/platform.h"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Logger;
class Prompter;

// ---- Stage type enum ----

enum class stage_type { vendor, fetch, resource, run, docker, premake5, package, group, disabled, binary, install, uninstall, clean, copy };

stage_type stage_from_string(const std::string &);
std::string to_string(stage_type);

// ---- Forward declarations ----

template<typename T> struct platform_entry;

// ---- Entry structs (pure data, shared for defaults and platform overrides) ----

struct artifact_entry
{
    std::string source;
    std::string dest;
    bool userdir = false;
    bool binary = false;
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
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::map<std::string, std::string> vars;

    // Per-platform overrides applied on top of defaults at resolution time.
    std::map<platform_type, platform_entry<fetch_entry>> platform;

    // Return a copy with platform overrides applied for the given platform.
    // Defined out-of-line after platform_entry<T> is complete.
    fetch_entry for_platform(platform_type pt) const;
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
    std::map<std::string, std::string> build_args;
};

struct premake5_entry
{
    std::string action = "gmake";
    std::optional<bool> make = true;
    std::optional<bool> strip = true;
    std::string target;
    std::string project;
    std::string config;
};

// ---- Platform entry template (adds build_context override to any entry type) ----

template<typename T>
struct platform_entry : T
{
    std::string build_context;   // empty = not set → use buildable_data default
};

// Out-of-line definition: fetch_entry::for_platform needs platform_entry<T> to be complete.
inline fetch_entry fetch_entry::for_platform(platform_type pt) const
{
    auto pit = platform.find(pt);
    if (pit == platform.end())
        return *this;

    auto r = *this;
    auto &o = pit->second;
    if (!o.url.empty())              r.url = o.url;
    if (!o.sha256.empty())           r.sha256 = o.sha256;
    if (!o.dest.empty())             r.dest = o.dest;
    if (!o.output_name.empty())      r.output_name = o.output_name;
    if (!o.include.empty())          r.include = o.include;
    if (!o.exclude.empty())          r.exclude = o.exclude;
    for (auto &[k, v] : o.vars)      r.vars[k] = v;
    r.platform.clear();
    return r;
}

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
    bool clean = false;
    std::vector<std::string> clean_paths;
};

struct buildable_data : stage_data
{
    std::vector<std::string> outputs;
    std::string build_context;
    bool clean = false;                       // opt-in for clean stage collection
    std::vector<std::string> clean_paths;     // extra paths to clean beyond outputs
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

// Copy/distribute local asset files from a source location into one or more
// destination paths. Mirrors a "init-assets" style script: validate the
// sources exist, then `cp -f` each into every destination (mkdir -p parents).
struct copy_file
{
    std::string source;                  // file to copy (relative to source_dir/base or prefixed)
    std::vector<std::string> dests;      // one or more destination paths
};

struct copy_entry
{
    std::string source_dir;              // optional base dir for relative `source`
    std::optional<bool> fail_if_missing; // unset → true (fail stage if a source is missing)
    std::vector<copy_file> files;
};

struct copy_data : stage_data
{
    copy_entry defaults;
    std::map<platform_type, platform_entry<copy_entry>> platform;
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

struct clean_data : stage_data
{
    std::vector<std::string> targets; // stages whose artifacts to clean (not resolved as deps)
    std::vector<std::string> paths;   // extra paths to remove
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

    // Stage map for actions that need to inspect other stages (e.g. clean).
    // Set by the resolver before dispatch.
    const std::unordered_map<std::string, stage_desc> *stages = nullptr;

    std::string resolve_path(const std::string &path) const;
};

inline std::string runtime::resolve_path(const std::string &path) const
{
    namespace fs = std::filesystem;
    auto result = path;
    auto p1 = result.find(path::cache);
    if (p1 != std::string::npos)
        result = (fs::path(result.substr(0, p1)) / cache_dir / result.substr(p1 + path::cache.size())).string();
    auto p2 = result.find(path::root);
    if (p2 != std::string::npos)
        result = (fs::path(result.substr(0, p2)) / root / result.substr(p2 + path::root.size())).string();
    return result;
}
