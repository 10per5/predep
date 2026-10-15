#pragma once

#include "action/action.h"
#include "cfg/config.h"
#include "data/stage.h"

class premake5_action : public action
{
public:
    static void parse(config_node &cfg, premake5_data &d);

    // Security: returns only the flags that are safe to append to the premake5
    // command. Unsafe entries (spaces / shell metacharacters / non-flag shape)
    // are excluded and never evaluated. See sanitize logic in the .cpp.
    static std::vector<std::string> sanitize_flags(const std::vector<std::string> &flags);

    bool is_resolved(const stage_desc &sd, runtime &ctx) const override;
    bool resolve(stage_desc &sd, runtime &ctx, std::string &error) override;
};
