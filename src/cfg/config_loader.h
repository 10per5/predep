#pragma once

#include "data/stage.h"
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class config_loader
{
    std::unordered_map<std::string, stage_desc> &m_stages;
    std::string &m_config_dir;
    std::string &m_error;
    std::string &m_main_stage;
    std::string &m_project;
    std::vector<std::string> &m_config_files;
public:
    config_loader(std::unordered_map<std::string, stage_desc> &stages,
                  std::string &config_dir,
                  std::string &error,
                  std::string &main_stage,
                  std::string &project,
                  std::vector<std::string> &config_files);

    bool load(const std::string &path);

    static std::string interpolate(const std::string &s,
                                   const std::map<std::string, std::string> &vars);
};
