#pragma once

#include <string>

namespace extract {

bool tar_gz(const std::string &archive, const std::string &dest_dir);
bool zip(const std::string &archive, const std::string &dest_dir);

}
