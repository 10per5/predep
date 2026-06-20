#include "action/disabled_action.h"
#include "logger/logger.h"

bool disabled_action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    (void)sd;
    (void)ctx;
    return false;
}

bool disabled_action::resolve(stage_desc &sd, runtime &ctx, std::string &error)
{
    (void)error;
    ctx.logger->info("Stage '" + sd.name + "' is not implemented yet.");
    return true;
}
