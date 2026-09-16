# Skill: Building with premake5, cmake, or make

## What it is

predep builds software with three stage types: `premake5`, `cmake`, and `make`. All run a build step, record an idempotency signature, and skip when nothing changed.

- `premake5` — runs premake5 to generate build files, then builds. Lighter, best for vanilla C++ projects.
- `cmake` — runs cmake configure + build. Use when the project/dependency is already CMake-based.
- `make` — runs `make` directly with structurally declared variables (no shell evaluation). For legacy/autotools projects that only ship a `Makefile`.

## Rule of thumb

- **Vanilla project you control** → `premake5`. predep itself builds with premake5 (ships a `premake5.lua`). Integrates with `build_context`/strip flow.
- **CMake-based dependency or project** → `cmake`. Don't wrap a CMake project in a premake5 shim.
- **Plain Makefile only** → `make`. No cmake/premake5 wrapper needed.

All build stages are reusable — a single stage can build the main project *or* a vendored library (linked via a vendor's `builder` field; see `vendor-libraries.md`).

## premake5 stage

```toml
[[stages]]
name = "build"
type = "premake5"
depends = ["vendor"]
config = "release"
outputs = ["root://bin/myapp"]
```

| Field | Default | Description |
| ----- | ------- | ----------- |
| `action` | `gmake` | Premake5 generator (`gmake`, `vs2022`, …) |
| `make` | `true` | Run the build after generation |
| `strip` | `true` | Strip the output binary |
| `target` | — | Specific output path to strip |
| `project` | root `project` key | Premake project name |
| `config` | — | Build config (`release`, `debug`) |

`premake5` is looked up on `PATH` and in `cache://bin`. Supports `[platform.*]` overrides for every field plus `build_context`.

`is_resolved()` always returns false (rebuilds each run).

## cmake stage

```toml
[[stages]]
name = "fmt-cmake"
type = "cmake"
source        = "root://vendor/fmt"
build_dir     = "root://vendor/fmt/build"
installPrefix = "root://vendor/prefix"
config        = "Release"
install        = true
flagsOn       = ["CMAKE_POSITION_INDEPENDENT_CODE"]
installPrefixVars = ["FMT_ROOT"]
configurable  = ["SOME_KEY=some_value"]
targets       = []                    # empty = default target
```

| Field | Default | Description |
| ----- | ------- | ----------- |
| `source` | — | Source directory |
| `build_dir` | `<source>/build` | CMake build dir |
| `config` | `Release` | `CMAKE_BUILD_TYPE` |
| `installPrefix` | `<source>/prefix` | `CMAKE_INSTALL_PREFIX` |
| `install` | `true` | Run `cmake --install` |
| `flagsOn` | — | `-D<flag>=ON` |
| `flagsOff` | — | `-D<flag>=OFF` |
| `configurable` | — | `-D<KEY>=<VALUE>` (raw `KEY=VALUE` strings) |
| `installPrefixVars` | — | `-D<var>=<installPrefix>` (expose prefix to dependents) |
| `targets` | — | Build targets (empty = default) |

The idempotency marker (`<build_dir>/.predepcmake`) records every input — source, build dir, prefix, flags, **and the pinned vendor ref** — so a ref change forces a rebuild. Supports `[platform.*]` overrides.

## make stage

```toml
[[stages]]
name = "pluto"
type = "make"
source        = "root://vendor/pluto"
installPrefix = "root://vendor/prefix"
targets       = []                    # empty = default target
jobs          = 0                     # 0 = auto (-j<nproc>), <0 = unlimited

[stages.variables]                    # → KEY=VALUE make args
DEBUG = "1"
```

| Field | Default | Description |
| ----- | ------- | ----------- |
| `source` | build_context cwd | Directory containing the `Makefile` |
| `targets` | — | Make targets (empty = default target) |
| `variables` | — | Table of `KEY = "value"` → `KEY=value` args |
| `installPrefix` | — | Passed as `<prefixVar>=<dir>`; when set, enables the install step |
| `prefixVar` | `PREFIX` | Make variable name receiving the install prefix |
| `install` | `true` | Run the `install` target (only when `installPrefix` is set) |
| `jobs` | `0` | Parallel jobs: `0` = auto (`-j<nproc>`), `<0` = unlimited (`-j`) |

The prefix variable is passed to the *build* invocation too, so Makefiles that bake install paths into compiled-in defaults behave correctly. Use `prefixVar` for Makefiles expecting a different name (e.g. lowercase `prefix`). `${VAR}` interpolation applies to `source`, `installPrefix`, variable keys/values, and targets. Supports `[platform.*]` overrides.

Idempotency: a `<source>/.predepmake` marker records every input including the pinned vendor ref, so a ref change forces a rebuild. `make` must be installed (see `predep-dockerfile.md`).

## Composing vendor → build

The recommended pattern for a vendored and built dependency:

```toml
[[vendor]]
name = "fmt"
url  = "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.tar.gz"
dest = "root://vendor/fmt/"
create_directory = true
builder = "fmt-cmake"            # auto-wires: fmt-cmake depends on vendor

[[stages]]
name = "fmt-cmake"
type = "cmake"
source        = "root://vendor/fmt"
installPrefix = "root://vendor/prefix"
installPrefixVars = ["FMT_ROOT"]

[[stages]]
name = "build"
type = "premake5"
depends = ["fmt-cmake"]          # main project consumes the built dep
config = "release"
outputs = ["root://bin/myapp"]
```

`FMT_ROOT` (from `installPrefixVars`) is visible to the `premake5` stage via `${FMT_ROOT}` interpolation in its config, so your `premake5.lua` can pick up the installed dependency.

## Gotchas

- **`premake5` / `cmake` / `make` binaries must be installed** for their stages; predep does not vendor its own toolchains. See `predep-dockerfile.md` for image setup.
- **`premake5` `is_resolved()` always returns false** (it rebuilds each run); `cmake` and `make` are idempotent via their signature markers.
- **Never put build logic in the vendor action.** Vendor entries only acquire source; the build lives in a `cmake`/`premake5`/`make` stage reachable via `builder`.
- **`build_context`** can point a build stage at a subdir (`"self"`, `"parent"`, or a relative path). Custom relative paths trigger a confirmation prompt unless `--privileged`.