#include "action/group_action.h"

bool group_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return true;
}

bool group_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    (void)sd;
    (void)ctx;
    (void)error;
    return true;
}
