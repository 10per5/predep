#pragma once

#include <string>
#include <vector>

namespace process {

int run(const std::string &cmd, const std::vector<std::string> &args = {},
        const std::string &cwd = "");
std::string capture(const std::string &cmd, const std::vector<std::string> &args = {});

// Like run() but also captures stderr text.
struct run_result {
    int code;
    std::string err;
};
run_result run_with_err(const std::string &cmd, const std::vector<std::string> &args = {},
                        const std::string &cwd = "");

// Cross-platform shell helpers
std::string shell();
std::string shell_cmd_flag();

#ifdef _WIN32
int run_elevated(const std::string &cmd, const std::vector<std::string> &args = {},
                 const std::string &cwd = "");
#endif

}
