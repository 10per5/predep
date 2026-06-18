#include "action/action.h"
#include "logger/prompter.h"
#include "sys/platform.h"
#include <filesystem>

namespace fs = std::filesystem;

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

std::string action::resolve_build_context(const stage_desc &sd,
                                           const runtime &ctx)
{
    auto bc = sd.build_context;
    auto pit = sd.platforms.find(ctx.platform);
    if (pit != sd.platforms.end() && !pit->second.build_context.empty())
        bc = pit->second.build_context;
    return bc;
}

std::string action::resolve_cwd(const stage_desc &sd, const runtime &ctx)
{
    auto bc = resolve_build_context(sd, ctx);
    auto config_root = ctx.root;

    if (bc.empty() || bc == "self")
        return config_root;
    if (bc == "parent")
        return fs::path(config_root).parent_path().string();

    return fs::absolute(fs::path(config_root) / bc).lexically_normal().string();
}

bool action::confirm_build_context(const stage_desc &sd, const runtime &ctx,
                                    std::string &error)
{
    auto bc = resolve_build_context(sd, ctx);
    if (bc.empty() || bc == "self" || bc == "parent")
        return true;

    if (ctx.prompter)
    {
        auto cwd = resolve_cwd(sd, ctx);
        std::vector<std::string> lines = {
            "  Stage '" + sd.name + "' uses custom build context:",
            "    " + bc + " -> " + cwd,
            "  Files outside the stage directory will be",
            "  visible to the build process."
        };
        if (!ctx.prompter->confirm_or_abort("BUILD CONTEXT WARNING", lines, error))
            return false;
    }

    return true;
}
