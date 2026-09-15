#pragma once

#include <functional>
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

// Like run_with_err() but invokes on_chunk with raw stderr data as it arrives
// (chunks may be partial lines / \r progress updates). on_chunk may be empty.
run_result run_with_err_stream(const std::string &cmd, const std::vector<std::string> &args,
                               const std::string &cwd,
                               const std::function<void(const std::string &)> &on_chunk);

// Cross-platform shell helpers
std::string shell();
std::string shell_cmd_flag();

#ifdef _WIN32
int run_elevated(const std::string &cmd, const std::vector<std::string> &args = {},
                 const std::string &cwd = "");
#endif

}
