#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct runtime;
struct stage_desc;

namespace security {

bool check_root_sudo(const runtime &ctx, std::string &error);

bool check_run_stages(const std::unordered_map<std::string, stage_desc> &stages,
                      const std::vector<std::string> &config_files,
                      runtime &ctx,
                      std::string &error);

bool check_path_safety(const std::unordered_map<std::string, stage_desc> &stages,
                       const std::vector<std::string> &config_files,
                       runtime &ctx,
                       std::string &error);

// True when `resolved` is the project root (root://) or lives beneath it.
// Source/dest path confinement for stages that must never write outside the
// project (e.g. copy). cache:// and escape paths are not in root.
bool is_in_root(const runtime &ctx, const std::string &resolved);

bool confirm_build_context(const stage_desc &sd,
                           const std::string &build_context,
                           const std::string &cwd,
                           runtime &ctx,
                           std::string &error);

// True when `token` is safe to embed in a command executed through the system
// shell (/bin/sh -c) — i.e. it cannot be parsed as shell syntax. Safe tokens
// contain only [A-Za-z0-9._:/+=-] with no whitespace or shell metacharacters.
// When `require_leading_dash` is true the token must also begin with '-' (used
// for `--flag`-style arguments). Used by the premake5 stage to validate its
// `flags`, `action`, `config`, and `project` before concatenation.
bool is_shell_safe(const std::string &token, bool require_leading_dash = false);

} // namespace security
