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

bool confirm_build_context(const stage_desc &sd,
                           const std::string &build_context,
                           const std::string &cwd,
                           runtime &ctx,
                           std::string &error);

} // namespace security
