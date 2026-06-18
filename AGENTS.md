# AGENTS.md — Architecture & Conventions

## Core Philosophy

predep is a stage-processing engine. It reads a declarative TOML manifest,
resolves stages and their dependencies in order (each stage = download / build / package step),
and produces artifacts. It is NOT tied to any specific project — the stages
you define in `predep.toml` determine what it does.

## Key Files

| File                                      | Responsibility                                                                                                     |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `predep/src/main.cpp`                     | Entry point: parse args, find config, load engine, resolve stage                                                   |
| `predep/src/cli/args.h/cpp`               | CLI argument parsing (CLI11)                                                                                       |
| `predep/src/cli/discovery.h/cpp`          | `project_root()`, `find_config()` — project root discovery                                                         |
| `predep/src/sys/platform.h/cpp`           | OS abstraction: file ops, arch detection, cache dirs, `exe_path()`                                                 |
| `predep/src/sys/process.h/cpp`            | Subprocess execution                                                                                               |
| `predep/src/sys/download.h/cpp`           | HTTP download with SHA256 verification                                                                             |
| `predep/src/sys/extract.h/cpp`            | Archive extraction (tar.gz, zip)                                                                                   |
| `predep/src/logger/logger.h/cpp`          | Logger base + ColorLogger/MonochromeLogger/MinifiedLogger/NullLogger + factory (color, levels, DI via runtime ctx) |
| `predep/src/data/stage.h`                 | Data structures: `stage_desc`, `vendor_entry`, `runtime`, polymorphic `stage_data` subtypes                        |
| `predep/src/data/const.h`                 | Path prefix constants (`root://`, `cache://`)                                                                      |
| `predep/src/cfg/config.h/cpp`             | Format-agnostic config node abstraction (`config_node`)                                                            |
| `predep/src/cfg/config_loader.h/cpp`      | TOML → `config_node`, include merging, `interpolate()`                                                             |
| `predep/src/action/action.h`              | Base class: `is_resolved()`, `resolve()`, `check_outputs()`                                                        |
| `predep/src/action/download_action.h/cpp` | Vendor/binary/resource download + extraction                                                                       |
| `predep/src/action/run_action.h/cpp`      | Shell command execution                                                                                            |
| `predep/src/action/docker_action.h/cpp`   | Docker build + cp                                                                                                  |
| `predep/src/action/package_action.h/cpp`  | Artifact assembly + archiving                                                                                      |
| `predep/src/action/group_action.h/cpp`    | No-op stage (always resolved)                                                                                      |
| `predep/src/engine/engine.h/cpp`          | Public API (`load_toml`, `resolve`, etc.) atop `config_loader` + `resolver`                                        |
| `predep/src/engine/resolver.h/cpp`        | DAG traversal + action dispatch via `m_actions` map                                                                |
| `predep/premake5.lua`                     | Build config + install/uninstall actions for predep itself                                                         |

## Include & Namespacing

`[[include]]` imports stages from other manifests. Each include gets a
namespace (default: the including file's directory name). Stages are
referenceable as `namespace::name`.

```toml
[[include]]
path = "subdir/predep.toml"

[[include]]
path = "other/predep.toml"
namespace = "custom"

[[include]]
path = "partial.toml"
only = ["stage-a", "stage-b"]
```

- `namespace` defaults to directory name (e.g. `subdir`), overridable
- `only` filters which stages to import from that file
- Internal dependencies between stages in the same file are automatically
  rewritten to use the prefixed names

## Variable Resolution (Interpolation)

`${VAR}` in URLs and paths is resolved in this order (higher overrides lower):

1. Entry-level flat keys (`version`, `arch`, etc. in `[[vendor]]`/`[[binary]]`)
2. Stage-level `[vars]` table keys
3. System defaults (lowest priority):
   - `PLATFORM` — from `--platform` flag or target OS
   - `ARCH` — host architecture (`x86_64`, `aarch64`)
   - `CPU` — mapped from ARCH (`x86_64` → `amd64`, `aarch64` → `arm64`)
   - `OS` — target OS
   - `EXE_SUFFIX` — `.exe` on Windows, empty on Unix

Only the first match of each `${VAR}` is replaced (single-pass).

Vars should be declared in a dedicated `[vars]` table:

```toml
[[stages]]
name = "sdl"
type = "vendor"
url = "https://example.com/sdl-${VERSION}.tar.gz"

[vars]
version = "2.30"
```

## Path Resolution

Two prefixes abstract filesystem layout:

- `root://` — resolves to the project root (configurable via `PREDEP_DIR`)
- `cache://` — resolves to the platform cache directory

All paths in config files use these prefixes. `runtime::resolve_path()`
translates them at resolution time.

## Stage Types

### `vendor` / `binary` / `resource`

Shorthand download stages. The type determines the default destination:

- `vendor` → `root://vendor/`
- `binary` → `cache://bin/`
- `resource` → `root://resources/`

Root-level `[[vendor]]`/`[[binary]]`/`[[resource]]` arrays merge into
stages of matching type (legacy/ergonomic pattern).

### `run`

Executes shell commands. Supports `[platform.xxx]` overrides. Falls back to
stage-level `commands` if no platform match.

### `docker`

Builds a Docker image, creates a container, copies out the `target` path.
Uses platform-specific `recipe`/`target`/`dest` overrides.

### `group`

No-op stage that only exists to depend on other stages. Resolves instantly.
Useful for grouping related stages into a single command.

### `package`

Copies artifacts into a dist directory and creates a compressed archive.

- `bundle` field controls the output archive name (default: `release`)
- `artifacts` array maps source paths to destinations within the archive

## premake5.lua — Build & Install

Two `premake5.lua` files provide project-specific actions:

### `predep/premake5.lua` (build predep itself)

- `install` copies `bin/predep` to `$PREFIX/bin/` (default: `/usr/local`)
- `uninstall` removes the binary and cleans empty parent dirs
- No manifest needed — just a single binary + optional symlink

## Conventions

- **No project-specific code in the engine.** `main.cpp` is a thin dispatcher.
  Project-specific logic lives in `predep.toml` stages or `premake5.lua`.
- **Stages are self-contained.** Each stage declares its own deps, outputs,
  and vars. No global state between stages.
- **Config is the source of truth.** Everything resolvable goes in TOML.
  CLI flags control _how_ to resolve, not _what_ to resolve.
- **Forward slashes everywhere.** Even on Windows. `cmd.exe` accepts them,
  and C++17 filesystem normalizes for the platform.

## Adding a New Stage Type

1. Add a `<name>_data` struct inheriting `stage_data` in `src/data/stage.h` with type-specific fields
2. Create an action class in `src/action/`
   - Inherit from `action`
   - Implement `static void parse(config_node &, your_data &)` to extract fields
   - Override `resolve()` — `dynamic_cast` `sd.data` to your data type
   - Override `is_resolved()` for type-specific resolution check (default checks `sd.outputs`)
3. Register the action in `src/engine/resolver.cpp` constructor: `m_actions["newtype"] = std::make_unique<newtype_action>();`
4. Add a creation branch in `src/cfg/config_loader.cpp` `parse_stages()`:

   ```cpp
   auto d = std::make_unique<your_data>();
   your_action::parse(elem, *d);
   sd.data = std::move(d);
   ```
