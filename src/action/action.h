#pragma once

#include "data/stage.h"
#include <string>

class action
{
public:
    virtual ~action() = default;

    virtual bool is_resolved(const stage_desc &sd, runtime &ctx) const;
    virtual bool resolve(stage_desc &sd, runtime &ctx, std::string &error) = 0;

    // --- shared helpers for all action types ---

    // Resolve the effective build_context value from stage_desc + platform override
    static std::string resolve_build_context(const stage_desc &sd,
                                              const runtime &ctx);

    // Compute the working directory based on build_context
    static std::string resolve_cwd(const stage_desc &sd, const runtime &ctx);

    // Warn about custom build_context and prompt for confirmation
    static bool confirm_build_context(const stage_desc &sd, const runtime &ctx,
                                       std::string &error);

protected:
    static bool check_outputs(const stage_desc &sd, const runtime &ctx);
};
