#pragma once

#include "data/stage.h"
#include <string>

class action
{
public:
    virtual ~action() = default;

    virtual bool is_resolved(const stage_desc &sd, runtime &ctx) const;
    virtual bool resolve(stage_desc &sd, runtime &ctx, std::string &error) = 0;

protected:
    static bool check_outputs(const stage_desc &sd, const runtime &ctx);
};
