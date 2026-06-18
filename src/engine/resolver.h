#pragma once

#include "action/action.h"
#include "data/stage.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

class resolver
{
    std::unordered_map<std::string, stage_desc> &m_stages;
    std::string &m_error;
    std::unordered_map<std::string, std::unique_ptr<action>> m_actions;
public:
    resolver(std::unordered_map<std::string, stage_desc> &stages,
             std::string &error);

    bool resolve(const std::string &name, runtime &ctx,
                 std::set<std::string> &visiting,
                 std::set<std::string> &resolved);

    bool resolve_all(runtime &ctx);

private:
    stage_desc *find(const std::string &name);
};
