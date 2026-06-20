#pragma once

#include "action/action.h"

class disabled_action : public action
{
public:
    bool is_resolved(const stage_desc &sd, runtime &ctx) const override;
    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
