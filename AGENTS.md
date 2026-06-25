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
| `predep/src/logger/prompter.h/cpp`        | Prompter base + InteractivePrompter/PrivilegedPrompter/NullPrompter + factory; user confirmation prompts               |
| `predep/src/data/stage.h`                 | Data structures: `stage_desc`, `fetch_entry`, `runtime`, polymorphic `stage_data` subtypes                        |
| `predep/src/data/const.h`                 | Path prefix constants (`root://`, `cache://`)                                                                      |
| `predep/src/cfg/config.h/cpp`             | Format-agnostic config node abstraction (`config_node`)                                                            |
| `predep/src/cfg/config_loader.h/cpp`      | TOML → `config_node`, include merging, `interpolate()`                                                             |
| `predep/src/action/action.h`              | Base class: `is_resolved()`, `resolve()`, `check_outputs()`, `resolve_cwd()`                                      |
| `predep/src/action/download_action.h/cpp` | Vendor/binary/resource download + extraction                                                                       |
| `predep/src/action/run_action.h/cpp`      | Shell command execution                                                                                            |
| `predep/src/action/docker_action.h/cpp`   | Docker build + cp                                                                                                  |
| `predep/src/action/package_action.h/cpp`  | Artifact assembly + archiving                                                                                      |
| `predep/src/action/install_action.h/cpp`  | Install artifacts to system prefix, with sudo escalation                                                           |
| `predep/src/action/uninstall_action.h/cpp`| Remove installed artifacts, clean empty dirs                                                                       |
| `predep/src/action/group_action.h/cpp`    | No-op stage (always resolved)                                                                                      |
| `predep/src/engine/engine.h/cpp`          | Public API (`load_toml`, `resolve`, etc.) atop `config_loader` + `resolver`                                        |
| `predep/src/engine/resolver.h/cpp`        | DAG traversal + action dispatch via `m_actions` map                                                                |
| `predep/premake5.lua`                     | Build config for predep itself                                                                                     |

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

1. Entry-level `[[vendor]]`/`[[binary]]`/`[[resource]]` variables (highest priority):
   - `[variables]` table — explicit per-entry declarations
   - `version` flat key — convenience shorthand for `[variables]version`
2. Stage-level `[vars]` table keys
3. System defaults (lowest priority):
   - `PLATFORM` — from `--platform` flag or target OS
   - `ARCH` — host architecture (`x86_64`, `aarch64`)
   - `CPU` — mapped from ARCH (`x86_64` → `amd64`, `aarch64` → `arm64`)
   - `OS` — target OS
   - `EXE_SUFFIX` — `.exe` on Windows, empty on Unix

All occurrences of `${VAR}` are replaced (multi-pass). Matching is case-insensitive:
`version = "0.14.0"` (lowercase in config) resolves `${VERSION}` (uppercase in URL).

Vars should be declared in a dedicated `[variables]` table at the entry level:

```toml
[[vendor]]
name = "sdl"
url = "https://example.com/sdl-${VERSION}.tar.gz"
dest = "root://vendor/"

[variables]
VERSION = "2.30"
```

Or using the `version` flat-key shorthand (equivalent):

```toml
[[vendor]]
name = "sdl"
url = "https://example.com/sdl-${VERSION}.tar.gz"
version = "2.30"
```

Stage-level vars are declared in a `[vars]` table on the stage:

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

## Install Config

The root-level `[install]` table configures system installation of build
artifacts. When present, the engine auto-generates `install` and `uninstall`
stages that copy artifacts to a system prefix and remove them respectively.

```toml
[install]
depends = ["build"]
artifacts = [
    { source = "root://bin/myapp", dest = "myapp" },
]
symlink = true
```

| Field      | Default         | Description                                              |
| ---------- | --------------- | -------------------------------------------------------- |
| `depends`  | —               | Stage dependencies (e.g. `["build"]`)                    |
| `dir`      | Platform-aware  | Install prefix (Unix: `/usr/local/bin`, Windows: `C:/Program Files/<project>`) |
| `artifacts`| — (required)    | Array of `{ source, dest }` — source is project/cache path, dest is relative to install dir |
| `symlink`  | `false`         | Create `/usr/local/bin/<project>` symlink pointing to the first artifact (Unix only, skipped when dir == `/usr/local/bin`) |

Platform overrides:

```toml
[install.platform.darwin]
dir = "/opt/myapp"
```

The install dir can be omitted entirely — the C++ action applies the correct
default per-platform (Unix: `/usr/local/bin`, Windows: `C:/Program Files/<project>`).
Artifacts are copied via `fs::copy()` with automatic `sudo cp` fallback when
the destination requires elevated privileges. See [docs/security.md](docs/security.md)
for the full sudo escalation model.

## Stage Types

### `vendor` / `fetch` / `resource`

Shorthand download stages. The type determines the default destination:

- `vendor` → `root://vendor/`
- `fetch` → unified download type (requires `fetch-type` field: `"binary"`, `"vendor"`, or `"resource"`)
- `resource` → `root://resources/`

Root-level `[[vendor]]`/`[[resource]]` arrays merge into stages of matching type.
`[[binary]]` (backward compat alias) and `[[fetch]]` merge into `type = "fetch"` stages
based on `fetch-type` matching.

```toml
[[stages]]
name = "get-tools"
type = "fetch"
fetch-type = "binary"      # fetches all [[binary]] entries

[[stages]]
name = "get-specific"
type = "fetch"
fetch-type = "binary"
assets = ["tool-a", "tool-b"]  # only fetch these specific entries
```

Root-level `[[fetch]]` entries carry their own `fetch-type`:

```toml
[[fetch]]
name = "shellcheck"
url = "https://example.com/shellcheck-${VERSION}.tar.gz"
fetch-type = "binary"

[[fetch]]
name = "sdl"
url = "https://example.com/sdl-${VERSION}.tar.gz"
fetch-type = "vendor"
version = "2.30"
```

### `run`

Executes shell commands. Supports `[platform.xxx]` overrides. Falls back to
stage-level `commands` if no platform match. Showing a security warning on run.

### `binary`

Executes a known binary with free-form arguments (a safer alternative to `run`
since it avoids shell evaluation). Uses `process::run()` (no shell). Checks
`cache://bin` then PATH, and exits early if the binary is not found.

```toml
[[stages]]
name = "hugo-build"
type = "binary"
binary = "hugo"
params = { contentDir = "../content", destination = "build" }
```

Supports `[platform.xxx]` overrides for `args`, `params`, and `build_context`.
Cross-platform: `platform::exe_name()` auto-appends `.exe` on Windows.
Future: binary metadata files will allow arg schema validation and
per-arg danger-level tagging.

**Security model:**
- `params` (`--key value` pairs) — DANGEROUS (no schema yet, roadmap)
- `args` (free-form argv tokens) — DANGEROUS (no validation)
- Both are still safer than `run` stages: `execvp()` avoids shell
  injection entirely (no `/bin/sh -c`, no `$()`, no backticks)
- Cross-platform: `platform::exe_name()` auto-appends `.exe` on Windows
- See [docs/security.md](docs/security.md) for full security model

### `disabled` / missing type

When no `type` is specified, defaults to `"disabled"`. These stages print
`"Stage '<name>' is not implemented yet."` and exit cleanly. Useful for
placeholders or work-in-progress stages.

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

### `install` / `uninstall`

Auto-generated from the root `[install]` config (not typically written as
`[[stages]]` entries). Copy artifacts to a system prefix and remove them.

- `dir` defaults per-platform: `/usr/local/bin` (Unix) or `C:/Program Files/<project>` (Windows)
- `artifacts` array maps project/cache paths to destinations under the install dir
- `symlink` creates `/usr/local/bin/<project>` pointing to the first artifact (Unix only, skipped when `dir == /usr/local/bin`)
- Copy/remove operations fall back to `sudo` automatically when the destination requires elevated privileges
- See [docs/security.md](docs/security.md) for the sudo escalation model

## build_context (All Stage Types)

Every stage can declare `build_context` to control the working directory during
execution. This is parsed at the stage level (not per-type) and applies to all
stages — `run`, `docker`, etc.

```toml
[[stages]]
name = "build-sdl"
type = "run"
build_context = "parent"           # run from parent of config dir
commands = ["./build-sdl.sh"]

[[stages]]
name = "docker-bundle"
type = "docker"
build_context = "../sdk"           # relative path from config dir
recipe = "Dockerfile"
target = "/out/sdk.tar.gz"
dest = "root://dist/"
```

Valid values:
- `"self"` (or empty) — use the stage's config directory (default)
- `"parent"` — use the parent of the config directory
- any relative path — resolved against the config directory

### Platform-specific Override

```toml
[platform.linux]
build_context = "../linux"
[platform.darwin]
build_context = "../mac"
```

### Confirmation Prompt

When `build_context` is a custom relative path (not `self`/`parent`), the user
is prompted to confirm. In non-interactive mode (no TTY), the prompt fails with
a message suggesting `--privileged` to override.

### Shared Helpers

`action::resolve_cwd(bc, ctx)` (`src/action/action.h`) computes the working directory
from build_context and runtime. `security::confirm_build_context(sd, ctx, error)`
(`src/security/security.h`) prompts the user to confirm out-of-dir execution.

### Prompter Class (logger/prompter.h)

User interaction follows the same pattern as Logger: abstract base with
implementations and a factory. `InteractivePrompter` checks for TTY, shows a
formatted warning box on stderr, and reads confirmation from stdin.

| Implementation       | Behavior                           |
| -------------------- | ---------------------------------- |
| `InteractivePrompter` | Shows warning box, reads stdin    |
| `PrivilegedPrompter`   | Interactive prompt (used with `--privileged`) |
| `NullPrompter`        | Auto-rejects (safe default)       |

The prompter is injected into `runtime` (like `Logger`).

## premake5.lua — Build

Two `premake5.lua` files provide project-specific actions:

### `predep/premake5.lua` (build predep itself)

- Build config only (install/uninstall moved to engine `[install]` stages)
- No manifest needed — just a single binary

## Conventions

- **No project-specific code in the engine.** `main.cpp` is a thin dispatcher.
  Project-specific logic lives in `predep.toml` stages or `premake5.lua`.
- **Stages are self-contained.** Each stage declares its own deps, outputs,
  and vars. No global state between stages.
- **Config is the source of truth.** Everything resolvable goes in TOML.
  CLI flags control _how_ to resolve, not _what_ to resolve.
- **Use `fs::path::operator/` for path construction.** This uses the platform's
  native separator (`\` on Windows, `/` on Unix). Never hardcode `/` or `\\` in
  path strings — always use `fs::path` concatenation.

## Enums

### `platform_type` (`src/data/const.h`)

```cpp
enum class platform_type { linux, darwin, windows };
```

Runtime detection via `current_platform()`. Conversion via `platform_from_string()` / `to_string()`.
Used as map key for all platform-override lookups.

### `stage_type` (`src/data/stage.h`)

```cpp
enum class stage_type { vendor, fetch, resource, run, docker, premake5, package, group, disabled, binary, installed, uninstalled };
```

+ `stage_from_string()`, `to_string()`. Replaces `std::string type` on `stage_desc`.

## Data Model

### Inheritance

```
stage_data                    ← dispatch vtable only
├── download_data             ← fetch-type, assets, entries + vars
├── buildable_data            ← outputs, build_context
│   ├── run_data              ← commands
│   ├── binary_data           ← binary name, args
│   ├── docker_data           ← recipe, target, dest
│   └── premake5_data         ← action, make, strip, target, project
├── package_data              ← artifacts, bundle
└── group_data                ← empty
```

### Entry structs (shared for defaults and platform overrides)

Each type defines an entry struct. Platform overrides use `platform_entry<T>` which inherits `T` and adds `build_context`:

```cpp
struct run_entry { vector<string> commands; };
struct binary_entry { vector<string> args; };

struct run_data : buildable_data {
    run_entry defaults;
    map<platform_type, platform_entry<run_entry>> platform;
};

struct binary_data : buildable_data {
    string binary_name;
    binary_entry defaults;
    map<platform_type, platform_entry<binary_entry>> platform;
};
```

`buildable_data::build_context` is the default; `platform_entry<T>::build_context` overrides per-platform.
`premake5_entry` uses `optional<bool>` for `make`/`strip` so platform entries can distinguish "not set" from "false".

### Stage descriptor

```cpp
struct stage_desc {
    string name, description;
    stage_type type;
    vector<string> depends;
    string config_dir;       // resolver: overrides ctx.root for included manifests
    unique_ptr<stage_data> data;
};
```

`outputs`, `build_context`, `vars`, `platforms` removed — moved to type hierarchy.

### Resolution

Each action's `resolve()`: start with `d->defaults`, merge fields from `d->platform[ctx.platform]` if present (non-empty strings, has_value optionals, non-empty vectors). Resolve `build_context` from platform entry override → `buildable_data` default.

---

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
