---
title: Building
weight: 13
---

# Building

## Premake5

Runs premake5 to generate build files, then runs make (or MSBuild on
Windows):

```toml
[[stages]]
name = "build"
type = "premake5"
depends = ["vendor"]
config = "release"
outputs = ["root://bin/predep"]
```

| Field | Default | Description |
|-------|---------|-------------|
| `action` | `"gmake"` | Premake5 generator action |
| `make` | `true` | Run build after generation |
| `strip` | `true` | Strip the output binary |
| `target` | — | Specific output path to strip |
| `project` | — | Project name (from `project` root key) |
| `config` | — | Build config (e.g., `release`, `debug`) |

Supports platform overrides for all fields plus `build_context`.

### When to use
- Building native projects that use premake5
- Vanilla / single-project C++ repos you control — preferred over `cmake` here

## CMake

Builds any CMake project: configure (`cmake -S -B`), build (`cmake --build`),
and optionally install (`cmake --install`). It is reusable for vendored libraries
*and* the main project, so no build code is duplicated.

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

`${VAR}` interpolation applies to `source`, `build_dir`, `installPrefix`,
`configurable` values, and `installPrefixVars`. Supports `[platform.*]`
overrides. Idempotency: a `<build_dir>/.predepcmake` marker records every input
including the pinned vendor ref, so a ref change forces a rebuild.

### When to use
- CMake-based dependencies or projects (don't wrap a CMake project in premake5)
- Building a vendored library linked from its `[[vendor]]` entry via `builder`

## Vendor → build (`builder` link)

A `[[vendor]]` entry with `builder = "stageName"` links to a build stage. The
engine auto-wires the dependency (the build stage runs *after* the source is
pulled), so an explicit `depends = ["vendor"]` on the build stage is optional:

```toml
[[vendor]]
name = "fmt"
url  = "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.tar.gz"
dest = "root://vendor/fmt/"
create_directory = true
builder = "fmt-cmake"          # fmt-cmake implicitly depends on "vendor"

[[stages]]
name = "fmt-cmake"
type = "cmake"
source = "root://vendor/fmt"
```

`builder` can point at a `cmake` **or** `premake5` stage interchangeably. If the
named stage is missing, predep errors at load time.
