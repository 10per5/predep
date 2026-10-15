// Unit tests for action::parse() field extraction.
// Part of predep-flags-plan.md change #6.
//
// NOTE: This file compiles against the test harness described in
// tests/PLAN.md (doctest.h, tests/main.cpp, tests/premake5.lua). That
// scaffolding is not yet present in the tree, so the tests are delivered
// here but only build/run once the harness exists. The fixture lives at
// tests/fixtures/premake5/predep.toml.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "action/premake5_action.h"
#include "cfg/config.h"
#include "data/stage.h"
#include "security/security.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    config_node find_stage(const config_node &root, const std::string &name)
    {
        for (auto &elem : root.get_array("stages"))
            if (elem.get_string("name") == name)
                return elem;
        return config_node{};
    }
}

TEST_CASE("premake5_action::parse reads flags into defaults")
{
    auto root = config_node::parse_file("fixtures/premake5/predep.toml");
    auto elem = find_stage(root, "build");
    REQUIRE(elem);

    premake5_data d;
    premake5_action::parse(elem, d);

    REQUIRE(d.defaults.action == "gmake");
    REQUIRE(d.defaults.flags.size() == 2);
    CHECK(d.defaults.flags[0] == "--tls");
    CHECK(d.defaults.flags[1] == "--cc=gcc");
}

TEST_CASE("premake5_action::parse lands platform.linux.flags in the platform entry")
{
    auto root = config_node::parse_file("fixtures/premake5/predep.toml");
    auto elem = find_stage(root, "build");
    REQUIRE(elem);

    premake5_data d;
    premake5_action::parse(elem, d);

    auto it = d.platform.find(platform_type::linux);
    REQUIRE(it != d.platform.end());
    // Platform override replaces the default list (consistent with scalars).
    REQUIRE(it->second.flags.size() == 2);
    CHECK(it->second.flags[0] == "--os=linux");
    CHECK(it->second.flags[1] == "--tls");
}

TEST_CASE("premake5_action::sanitize_flags keeps only shell-safe --word tokens")
{
    // premake_cmd runs via /bin/sh -c, so spaces / metacharacters are an
    // injection vector and must be discarded (never evaluated).
    std::vector<std::string> in = {
        "--tls",            // ok: --word
        "--cc=gcc",         // ok: --word=value
        "--to=build/linux", // ok: path value with '/'
        "--bad; rm -rf ~",  // injected command separator
        "notaflag",         // not a flag (no leading '-')
        "--spaced value",   // contains a space
        "--evil$(id)",      // command substitution
        "--redir>out",      // shell redirection
    };
    auto out = premake5_action::sanitize_flags(in);

    REQUIRE(out.size() == 3);
    CHECK(out[0] == "--tls");
    CHECK(out[1] == "--cc=gcc");
    CHECK(out[2] == "--to=build/linux");
}

TEST_CASE("security::is_shell_safe rejects shell-unsafe scalars")
{
    // action / config / project are concatenated into the `sh -c` command, so
    // they must each be a single shell-safe word. The shared `security` util is
    // the single source of truth for both flag and scalar validation.
    CHECK(security::is_shell_safe("gmake"));      // ok
    CHECK(security::is_shell_safe("vs2022"));     // ok
    CHECK(security::is_shell_safe("release"));    // ok
    CHECK(security::is_shell_safe("my-app"));     // ok (hyphen)

    CHECK_FALSE(security::is_shell_safe(""));          // empty
    CHECK_FALSE(security::is_shell_safe("gmake; rm")); // command separator
    CHECK_FALSE(security::is_shell_safe("rel ease"));  // space
    CHECK_FALSE(security::is_shell_safe("c$(id)"));    // substitution
    CHECK_FALSE(security::is_shell_safe("a>b"));       // redirection

    // The same util, with require_leading_dash, is what sanitize_flags uses.
    CHECK(security::is_shell_safe("--cc=gcc", /*require_leading_dash=*/true));
    CHECK_FALSE(security::is_shell_safe("gmake", /*require_leading_dash=*/true)); // no dash
}
