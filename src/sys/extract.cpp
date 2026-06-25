#include "sys/extract.h"
#include "sys/process.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace extract {

static bool check_tool(const std::string &name)
{
#ifdef _WIN32
    auto rc = process::run(process::shell(), {process::shell_cmd_flag(), "where " + name + " >nul 2>nul"});
#else
    auto rc = process::run(process::shell(), {process::shell_cmd_flag(), "which " + name});
#endif
    return rc == 0;
}

bool tar_gz(const std::string &archive, const std::string &dest_dir,
            const std::vector<std::string> &include,
            const std::vector<std::string> &exclude)
{
    if (!check_tool("tar"))
    {
        std::cerr << "error: 'tar' not found — install tar or use a pre-extracted source\n";
        return false;
    }
    fs::create_directories(dest_dir);
    std::vector<std::string> args = {"-xzf", archive, "-C", dest_dir};
    for (auto &exc : exclude)
        args.push_back("--exclude=" + exc);
    for (auto &inc : include)
        args.push_back(inc);
    return process::run("tar", args) == 0;
}

bool zip(const std::string &archive, const std::string &dest_dir,
         const std::vector<std::string> &include,
         const std::vector<std::string> &exclude)
{
    fs::create_directories(dest_dir);
#ifdef _WIN32
    auto rc = process::run("powershell", {
        "Expand-Archive", "-Path", archive,
        "-DestinationPath", dest_dir, "-Force"
    });
    if (rc != 0)
        std::cerr << "error: failed to extract zip — ensure PowerShell is available\n";
    return rc == 0;
#else
    if (!check_tool("unzip"))
    {
        std::cerr << "error: 'unzip' not found — install unzip (apt install unzip, brew install unzip)\n";
        return false;
    }
    std::vector<std::string> args = {"-q", "-o", archive, "-d", dest_dir};
    for (auto &inc : include)
        args.push_back(inc);
    if (!exclude.empty())
    {
        args.push_back("-x");
        for (auto &exc : exclude)
            args.push_back(exc);
    }
    return process::run("unzip", args) == 0;
#endif
}

}
