#pragma once

#include <string>
#include <vector>

namespace extract {

bool tar_gz(const std::string &archive, const std::string &dest_dir,
            const std::vector<std::string> &include = {},
            const std::vector<std::string> &exclude = {});
bool zip(const std::string &archive, const std::string &dest_dir,
         const std::vector<std::string> &include = {},
         const std::vector<std::string> &exclude = {});

}
