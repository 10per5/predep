#pragma once

#include "action/action.h"
#include "cfg/config.h"
#include "data/stage.h"

class docker_action : public action
{
public:
    static void parse(config_node &cfg, docker_data &d);

    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
