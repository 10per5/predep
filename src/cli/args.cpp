#include "cli/args.h"
#include "sys/platform.h"
#include "CLI11/CLI11.hpp"
#include <cstdlib>
#include <iostream>

parsed_args parse_args(int argc, char **argv)
{
    parsed_args args;
    args.target_os = platform::os();

    CLI::App app("Project dependency & build tool");

    app.add_flag("--debug", args.debug, "Verbose logging");
    app.add_flag("--force", args.force, "Skip confirmation prompts, not implemented for normal builds for security purposes");
    app.add_option("--platform", args.platform_override, "Target platform (linux/darwin/windows)");
    app.add_option("--config", args.config_path, "Use specific config file");
    app.add_option("--os", args.target_os, "Target OS for packaging");
    app.add_flag("--list", args.list, "List available stages");
    app.add_option("--format", args.format, "Output format: auto, mono, minified, none");
    app.add_option("--jobs,-j", args.jobs, "Max concurrent stages (default 1, WIP)");
    app.add_option("--parent-limit", args.parent_limit,
        "Max parent dirs to search for predep.toml (0 = cwd only, default 0)");

    app.add_option("stage", args.command, "Stage name or 'package'")
        ->take_last();

    app.set_help_flag("--help,-h", "Show this help");

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        std::exit(app.exit(e));
    }

    return args;
}
