#include "engine/resolver.h"
#include "action/download_action.h"
#include "action/run_action.h"
#include "action/binary_action.h"
#include "action/premake5_action.h"
#include "action/docker_action.h"
#include "action/package_action.h"
#include "action/install_action.h"
#include "action/uninstall_action.h"
#include "action/group_action.h"
#include "action/disabled_action.h"
#include "action/clean_action.h"
#include "action/copy_action.h"
#include "logger/logger.h"

resolver::resolver(
    std::unordered_map<std::string, stage_desc> &stages,
    std::string &error)
    : m_stages(stages)
    , m_error(error)
{
    m_actions[stage_type::vendor]   = std::make_unique<download_action>();
    m_actions[stage_type::fetch]    = std::make_unique<download_action>();
    m_actions[stage_type::resource] = std::make_unique<download_action>();
    m_actions[stage_type::run]      = std::make_unique<run_action>();
    m_actions[stage_type::premake5] = std::make_unique<premake5_action>();
    m_actions[stage_type::docker]   = std::make_unique<docker_action>();
    m_actions[stage_type::package]  = std::make_unique<package_action>();
    m_actions[stage_type::group]    = std::make_unique<group_action>();
    m_actions[stage_type::disabled] = std::make_unique<disabled_action>();
    m_actions[stage_type::binary]   = std::make_unique<binary_action>();
    m_actions[stage_type::install]   = std::make_unique<install_action>();
    m_actions[stage_type::uninstall] = std::make_unique<uninstall_action>();
    m_actions[stage_type::clean]     = std::make_unique<clean_action>();
    m_actions[stage_type::copy]      = std::make_unique<copy_action>();
}

stage_desc *resolver::find(const std::string &name)
{
    auto it = m_stages.find(name);
    return it != m_stages.end() ? &it->second : nullptr;
}

bool resolver::resolve(const std::string &name, runtime &ctx,
                        std::set<std::string> &visiting,
                        std::set<std::string> &resolved)
{
    if (resolved.find(name) != resolved.end())
        return true;

    if (visiting.find(name) != visiting.end())
    {
        m_error = "circular dependency detected: " + name;
        return false;
    }

    auto *sd = find(name);
    if (!sd)
    {
        m_error = "unknown stage: " + name;
        return false;
    }

    visiting.insert(name);

    for (auto &dep : sd->depends)
    {
        if (!resolve(dep, ctx, visiting, resolved))
            return false;
    }

    visiting.erase(name);

    auto saved_root = ctx.root;
    if (!sd->config_dir.empty())
        ctx.root = sd->config_dir;

    auto it = m_actions.find(sd->type);
    if (it == m_actions.end())
    {
        ctx.root = saved_root;
        m_error = "unknown stage type: " + to_string(sd->type);
        return false;
    }

    auto &act = *it->second;

    if (!act.is_resolved(*sd, ctx))
    {
        if (ctx.logger)
            ctx.logger->debug("resolving stage: " + name);
        if (!act.resolve(*sd, ctx, m_error))
        {
            ctx.root = saved_root;
            if (ctx.logger)
                ctx.logger->error("stage '" + name + "' failed: " + m_error);
            return false;
        }
    }
    else
    {
        if (ctx.logger)
            ctx.logger->debug("stage already resolved: " + name);
    }

    ctx.root = saved_root;
    resolved.insert(name);
    return true;
}

bool resolver::resolve_all(runtime &ctx)
{
    if (ctx.max_concurrency > 1 && ctx.logger)
        ctx.logger->warn(
            "parallel stage execution requested (--jobs="
            + std::to_string(ctx.max_concurrency)
            + ") but not yet implemented — running sequentially.\n"
            "  thread-safety blockers: resolver mutates ctx.root,\n"
            "  action resolve() mutates stage_desc.data, multi-stage\n"
            "  download actions may contend on cache/archives.\n"
            "  Future design: vendor stages could build different projects\n"
            "  concurrently (SDL + FFmpeg + Lua), but large interdependent\n"
            "  deps may need a monolithic aggregate stage to avoid redundant\n"
            "  downloads and extract conflicts in cache://archives/.");

    std::set<std::string> visiting;
    std::set<std::string> resolved;

    for (auto &[name, _] : m_stages)
    {
        if (resolved.find(name) == resolved.end())
        {
            if (!resolve(name, ctx, visiting, resolved))
                return false;
        }
    }
    return true;
}
