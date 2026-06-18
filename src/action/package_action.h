#pragma once

#include "action/action.h"
#include "cfg/config.h"
#include "data/stage.h"

class package_action : public action
{
public:
    static void parse(config_node &cfg, package_data &d);

    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
