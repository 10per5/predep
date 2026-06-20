#pragma once

#include "action/action.h"
#include "cfg/config.h"
#include "data/stage.h"

class uninstall_action : public action
{
public:
    static void parse(config_node &cfg, uninstall_data &d);

    bool is_resolved(const stage_desc &sd, runtime &ctx) const override;
    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
