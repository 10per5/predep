#pragma once

#include <string>

struct parsed_args
{
    bool debug = false;
    bool privileged = false;
    bool list = false;
    int jobs = 1;
    int parent_limit = 0;
    std::string format;
    std::string platform_override;
    std::string config_path;
    std::string target_os;
    std::string command;
};

parsed_args parse_args(int argc, char **argv);
