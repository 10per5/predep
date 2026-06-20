#include "action/action.h"
#include "sys/platform.h"
#include <filesystem>

namespace fs = std::filesystem;

bool action::is_resolved(const stage_desc &sd, runtime &ctx) const
{
    return check_outputs(sd, ctx);
}

bool action::check_outputs(const stage_desc &sd, const runtime &ctx)
{
    auto *bd = dynamic_cast<const buildable_data*>(sd.data.get());
    if (!bd || bd->outputs.empty())
        return false;

    for (auto &out : bd->outputs)
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

std::string action::resolve_cwd(const std::string &bc, const runtime &ctx)
{
    auto config_root = ctx.root;

    if (bc.empty() || bc == "self")
        return config_root;
    if (bc == "parent")
        return fs::path(config_root).parent_path().string();

    return fs::absolute(fs::path(config_root) / bc).lexically_normal().string();
}
