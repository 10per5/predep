#pragma once

#include "action/action.h"
#include "cfg/config.h"
#include "data/stage.h"

class make_action : public action
{
public:
    static void parse(config_node &cfg, make_data &d);

    bool is_resolved(const stage_desc &sd, runtime &ctx) const override;
    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
