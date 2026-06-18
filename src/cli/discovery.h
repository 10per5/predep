#pragma once

#include <string>

class Logger;

std::string project_root(int parent_limit = 0);
std::string find_config(const std::string &root, Logger &logger);
