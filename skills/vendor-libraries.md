# Skill: Vendoring third-party libraries with predep

## What it is

predep pulls third-party libraries into your project tree under `root://vendor/`
and (optionally) builds them. A single `[[vendor]]` entry describes one
dependency. Two acquisition modes are supported:

- **Archive/file download** — `url` + `sha256`. Tarballs/zip auto-extract.
- **Git clone pin** — `repo` + `ref` (commit SHA or tag). See
  `vendor-git-ref.md` for that mode.

This skill covers the common case: vendor a library and compile it with either
a `cmake` or `premake5` build stage, wired together with the `builder` link.

## predep.toml shape

```toml
main = "build"
project = "myapp"

[[stages]]
name = "vendor"
type = "vendor"

[[vendor]]
name = "fmt"
url  = "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.tar.gz"
sha256 = "9b8e9a29a2d8d7a3a4f3b1b1f3a3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1"  # optional
dest = "root://vendor/fmt/"
create_directory = true          # tarball keeps its inner top dir; flatten it
builder = "fmt-build"            # link to the build stage below

[[stages]]
name = "fmt-build"
type = "cmake"
source        = "root://vendor/fmt"
build_dir     = "root://vendor/fmt/build"
installPrefix = "root://vendor/prefix"
install        = true
configurable  = ["CMAKE_POSITION_INDEPENDENT_CODE=ON"]

[[stages]]
name = "build"
type = "premake5"
depends = ["fmt-build"]
config = "release"
outputs = ["root://bin/myapp"]
```

## How the `builder` link works

`builder = "fmt-build"` on the `[[vendor]]` entry points at a build stage.
The engine auto-wires the dependency: the build stage runs *after* the vendor
source is pulled, so you do **not** need an explicit
`depends = ["vendor"]` on the build stage. If the build stage is missing,
predep errors at load time.

The link is build-system agnostic — `builder` can point at a `cmake` stage
(prefer this for CMake-based libraries) or a `premake5` stage (good for
libraries that already ship a `premake5.lua`). See `building-stages.md`.

## Key fields on a `[[vendor]]` entry

| Field | Default | Description |
| ----- | ------- | ----------- |
| `name` | — | Entry identifier (also the default output filename) |
| `url` | — | Archive/file URL (supports `${VAR}`) |
| `sha256` | — | Verification hash. **Omit to skip verification** — on first download predep logs the computed hash so you can paste it in. |
| `dest` | `root://vendor/` | Destination directory |
| `extract` | auto | Extract archives (`.tar.gz`/`.tgz`/`.zip` auto-detected) |
| `create_directory` | `false` | Flatten the tarball's top-level dir into `dest` |
| `output_name` | `name` | Rename the downloaded file |
| `include` / `exclude` | — | Glob filters applied to extracted files (string or array) |
| `version` / `variables` | — | Per-entry `${VAR}` interpolations |
| `builder` | — | Name of the build stage that compiles this entry |

## Gotchas

- **Archive layout:** archive vendors preserve the tarball's own top-level dir
  (e.g. `fmt-11.0.2/`). Set `dest` to the *parent* directory and turn on
  `create_directory` to flatten, or set `build_dir`/`source` to the full
  `root://vendor/fmt/fmt-11.0.2` path.
- **Missing `sha256` is fine for iteration.** predep logs the computed hash; add
  it for supply-chain integrity once the download is confirmed good.
- **Idempotency:** re-running `predep vendor` is a no-op when the extracted tree
  already matches `sha256` (or, for git, the `.predepgit` ref marker matches).
- **Reconcile include paths.** `dest`/flattening must line up with what your
  `premake5.lua` actually `#include`s — the #1 cause of "header not found".
