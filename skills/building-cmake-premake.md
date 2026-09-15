# Skill: Building with premake5 or cmake

## What it is

predep builds software with two stage types: `premake5` and `cmake`. Both run a
configure/generate step then a build step, record an idempotency signature, and
skip when nothing changed. This skill explains both and the rule: **prefer
premake5 on vanilla / single-project C++ repos; reach for cmake when the
dependency or project is already a CMake project.**

## Rule of thumb

- **Vanilla project you control** → `premake5`. predep itself builds with
  `premake5` (it ships a `premake5.lua`). It's lighter, generates `Makefile`s
  directly, and integrates with predep's `build_context`/strip flow.
- **CMake-based dependency or project** → `cmake`. Don't wrap a CMake project in
  a `premake5` shim; use the `cmake` stage and point `source` at the vendor dir.

Both build stages are reusable — a single `cmake`/`premake5` stage can build the
main project *or* a vendored library (linked via a vendor's `builder` field; see
`vendor-libraries.md`).

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

`premake5` is looked up on `PATH` and in `cache://bin`. Supports `[platform.*]`
overrides for every field plus `build_context`.

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
installPrefixVars = ["FMT_ROOT"]      # → -DFMT_ROOT=<installPrefix>
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

The idempotency marker (`<build_dir>/.predepcmake`) records every input — source,
build dir, prefix, flags, **and the pinned vendor ref** — so a ref change forces
a rebuild. Supports `[platform.*]` overrides.

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

`FMT_ROOT` (from `installPrefixVars`) is visible to the `premake5` stage via
`${FMT_ROOT}` interpolation in its config, so your `premake5.lua` can pick up the
installed dependency.

## Gotchas

- **`premake5` and `cmake` binaries must be installed** for their stages; predep
  does not vendor its own toolchains. See `predep-dockerfile.md` for image setup.
- **`premake5` `is_resolved()` always returns false** (it rebuilds each run);
  `cmake` is idempotent via its signature marker.
- **Never put build logic in the vendor action.** Vendor entries only acquire
  source; the build lives in a `cmake`/`premake5` stage reachable via `builder`.
- **`build_context`** can point a build stage at a subdir (`"self"`, `"parent"`,
  or a relative path). Custom relative paths trigger a confirmation prompt unless
  `--privileged`.
