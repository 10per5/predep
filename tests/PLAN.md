# Test Plan — predep

## Structure

```
tests/
├── premake5.lua            # Self-contained build for test runner
├── main.cpp                # doctest main()
├── ut/                     # Unit tests (fast, isolated, no IO)
│   ├── test_config.cpp     # TOML parsing, config_node API
│   ├── test_interpolate.cpp# Variable resolution order
│   ├── test_discovery.cpp  # project_root(), find_config()
│   ├── test_paths.cpp      # root://, cache:// resolution
│   ├── test_logger.cpp     # make_logger() format selection
│   └── test_actions.cpp    # action parse() functions per type
├── it/                     # Integration tests (full pipeline)
│   ├── test_cli.cpp        # parse_args() flag handling
│   ├── test_resolve.cpp    # Load config → resolve single stage
│   ├── test_pipeline.cpp   # Multi-stage DAG with deps
│   └── test_errors.cpp     # Invalid configs, missing files
├── fixtures/               # Test TOML configs (no sgtwiki refs)
│   ├── basic/
│   │   └── predep.toml     # Simple group stage
│   ├── interpolate/
│   │   └── predep.toml     # Vendor with vars + interpolation
│   ├── include/
│   │   ├── main.toml       # [[include]] test
│   │   └── sub/
│   │       └── predep.toml # Included sub-config
│   └── invalid/
│       ├── bad_syntax.toml # Malformed TOML
│       ├── no_type.toml    # Stage missing type field
│       └── no_name.toml    # Stage missing name field
├── vendor/
│   └── doctest/
│       └── doctest.h       # Header-only test framework
└── README.md               # Build & run instructions

## Framework: doctest

- Header-only, single-file (`vendor/doctest/doctest.h`)
- No linking, no runtime deps
- `#define DOCTEST_CONFIG_IMPLEMENT` in main.cpp

## Build: tests/premake5.lua

- Generates `tests/Makefile` via `premake5 gmake`
- Output: `tests/bin/predep-test`
- Compiles `../src/**.cpp` (excluding `../src/main.cpp`) + `ut/**.cpp` + `it/**.cpp`
- Include paths: `../src`, `../vendor`, `vendor/doctest`

## Tests

### Unit Tests (ut/)

| File | Tests |
|------|-------|
| test_config.cpp | config_node parse, has/get/array/table ops |
| test_interpolate.cpp | ${VAR} resolution order, entry→stage→system fallback, unknown vars |
| test_discovery.cpp | project_root() from cwd/parent, find_config() with/without markers |
| test_paths.cpp | root:// and cache:// prefix resolution, relative path passthrough |
| test_logger.cpp | make_logger() format selection, debug gate, NullLogger no-op |
| test_actions.cpp | run_action::parse, download_action::parse field extraction |

### Integration Tests (it/)

| File | Tests |
|------|-------|
| test_cli.cpp | parse_args() with --format, --debug, --platform, --parent-limit, positional |
| test_resolve.cpp | engine load_toml + resolve basic/group, check stage_names, has_stage |
| test_pipeline.cpp | Multi-stage DAG with depends, include merging, namespaced refs |
| test_errors.cpp | Missing/invalid config files, bad TOML syntax, unknown stages |

### Fixture Configs

- **basic/predep.toml** — Group stage, no deps
- **interpolate/predep.toml** — Vendor download with ${NAME}-${VERSION} URL
- **include/main.toml** + **sub/predep.toml** — [[include]] with namespace
- **invalid/bad_syntax.toml** — Broken TOML syntax
- **invalid/no_type.toml** — Stage without `type`
- **invalid/no_name.toml** — Stage without `name`

## Build & Run

```bash
cd predep/tests
premake5 gmake && make -j$(nproc)
./bin/predep-test          # all tests
./bin/predep-test -s "test_config*"   # filtered
./bin/predep-test -c                  # list test count
```

## Constraints

- No bash scripts — all tests are C++
- No references to sgtwiki or external projects
- Self-contained: when predep migrates to its own repo, tests/ moves as-is
