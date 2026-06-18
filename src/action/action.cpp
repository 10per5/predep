#include "action/action.h"
#include "sys/platform.h"

bool action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    return check_outputs(sd, ctx);
}

bool action::check_outputs(const stage_desc &sd, const runtime &ctx)
{
    if (!sd.outputs.empty())
    {
        for (auto &out : sd.outputs)
        {
            auto path = ctx.resolve_path(out);
            if (platform::file_exists(path))
                continue;
            if (path.back() == '/' || path.find('.') == std::string::npos)
                if (platform::dir_exists(path))
                    continue;
            return false;
        }
        return true;
    }
    return false;
}
