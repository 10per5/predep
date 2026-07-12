#include "engine/engine.h"
#include "cfg/config_loader.h"
#include "cfg/prefs.h"
#include "engine/resolver.h"
#include "data/stage.h"
#include "logger/logger.h"
#include "security/security.h"
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct engine::impl
{
    std::unordered_map<std::string, stage_desc> stages;
    std::string config_dir;
    std::string config_path;
    std::vector<std::string> config_files;
    std::string error;
    std::string main_stage_name;
    std::string project;
    config_loader loader{stages, config_dir, error, main_stage_name, project, config_files};
    resolver resv{stages, error};

    bool has_stage(const std::string &name) const
    {
        return stages.find(name) != stages.end();
    }
};

engine::engine()
    : m_impl(std::make_unique<impl>()) {}

engine::~engine() = default;

bool engine::load_toml(const std::string &path)
{
    m_impl->stages.clear();
    m_impl->config_dir = fs::absolute(fs::path(path).parent_path()).string();
    m_impl->config_files.clear();
    m_impl->config_files.push_back(path);
    return m_impl->loader.load(path);
}

bool engine::load_lua(const std::string &path)
{
    m_impl->error = "Lua loader not implemented yet";
    (void)path;
    return false;
}

std::vector<std::string> engine::stage_names() const
{
    std::vector<std::string> names;
    for (auto &[name, _] : m_impl->stages)
        names.push_back(name);
    return names;
}

bool engine::has_stage(const std::string &name) const
{
    return m_impl->has_stage(name);
}

bool engine::resolve(const std::string &stage_name, runtime &ctx)
{
    ctx.project = m_impl->project;
    auto expired = prefs::sanitize();
    if (expired > 0 && ctx.logger)
        ctx.logger->info("Cleared " + std::to_string(expired) + " expired trust entries (see " + prefs::prefs_path() + ")");

    if (!security::check_path_safety(m_impl->stages, m_impl->config_files, ctx, m_impl->error))
        return false;

    if (!security::check_run_stages(m_impl->stages, m_impl->config_files, ctx, m_impl->error))
        return false;

    ctx.stages = &m_impl->stages;

    std::set<std::string> visiting;
    std::set<std::string> resolved;
    return m_impl->resv.resolve(stage_name, ctx, visiting, resolved);
}

bool engine::resolve_all(runtime &ctx)
{
    ctx.project = m_impl->project;
    ctx.stages = &m_impl->stages;
    auto expired = prefs::sanitize();
    if (expired > 0 && ctx.logger)
        ctx.logger->info("Cleared " + std::to_string(expired) + " expired trust entries (see " + prefs::prefs_path() + ")");

    if (!security::check_path_safety(m_impl->stages, m_impl->config_files, ctx, m_impl->error))
        return false;

    if (!security::check_run_stages(m_impl->stages, m_impl->config_files, ctx, m_impl->error))
        return false;

    return m_impl->resv.resolve_all(ctx);
}

std::string engine::main_stage() const
{
    return m_impl->main_stage_name;
}

std::string engine::last_error() const
{
    return m_impl->error;
}
