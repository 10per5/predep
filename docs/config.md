---
title: Config Format
weight: 20
---

# Config format

predep uses TOML manifests. Each project declares stages in `predep.toml`.
The root manifest composes subproject manifests with `[[include]]`.

## Root manifest

```toml
main = "package"
project = "myapp"

[[include]]
path = "editor/predep.toml"

[[include]]
path = "hugo-view/predep.toml"

[[include]]
path = "gui/predep.toml"
namespace = "gui"

[[stages]]
name = "build"
type = "group"
depends = ["editor::editor-assets", "gui::gui-binary"]
```

| Key | Description |
| --- | ----------- |
| `main` | Default stage to resolve when none is specified on the CLI |
| `project` | Project name — used as default for premake5 project field, install directory on Windows, and symlink name |

## Include & namespacing

`[[include]]` imports stages from other manifests:

```toml
[[include]]
path = "subdir/predep.toml"
namespace = "custom"
only = ["stage-a", "stage-b"]
```

| Field | Description |
| ----- | ----------- |
| `path` | Path to included manifest (relative to including file) |
| `namespace` | Namespace prefix (default: including file's directory name) |
| `only` | Optional — only import specific stages |

Stages from included files are referenceable as `namespace::name`.
Internal dependencies within a file are automatically rewritten with the
prefixed names.

## Stage definitions

Each `[[stages]]` entry has these common fields:

| Field | Description |
| ----- | ----------- |
| `name` | Unique stage name |
| `type` | Stage type (vendor, fetch, resource, run, binary, docker, premake5, package, group) |
| `depends` | Array of stage dependencies (use `namespace::name` for cross-manifest) |
| `description` | Optional human-readable label |
| `outputs` | Array of expected output paths (when all exist, stage is skipped) |
| `build_context` | Working directory: `"self"` (default), `"parent"`, or a relative path |

Type-specific fields are documented in [stages/](stages/).

### Outputs example

```toml
[[stages]]
name = "build"
type = "premake5"
outputs = ["root://bin/myapp"]
```

When all `outputs` exist on disk, the stage is skipped on re-run.

## Platform overrides

Platform overrides can be specified per-stage using dotted keys or inline
tables within the stage entry:

```toml
[[stages]]
name = "build-app"
type = "run"
commands = ["make -j$(nproc)"]
platform.windows.commands = ["msbuild app.sln /p:Configuration=Release"]
platform.darwin.commands = ["xcodebuild -project myapp.xcodeproj"]
```

Equivalently as an inline table:

```toml
[[stages]]
name = "build-app"
type = "run"
commands = ["make -j$(nproc)"]
platform = { windows = { commands = ["msbuild app.sln /p:Configuration=Release"] } }
```

Accepted OS keys:
- `linux` — Linux
- `darwin`, `macos`, `osx`, `mac` — macOS
- `windows`, `win32`, `win64`, `win` — Windows

Platform overrides are **partial** — only the fields you specify differ from
the defaults. You can also override `build_context` per-platform:
`platform.darwin.build_context = "../mac"`

### Root-level stage overrides

Platform overrides can also be defined at the root level and applied to
specific stages by name, as used in the project's own `predep.toml`:

```toml
[platform.win32.stage.docker]
recipe = "images/win32.Dockerfile"
target = "/src/predep/bin/predep.exe"
dest = "root://bin/"

[platform.darwin.stage.docker]
recipe = "images/darwin.Dockerfile"
target = "/src/predep/bin/predep"
dest = "root://bin/"
```

Applied at config load time, before per-stage platform resolution.
Supported for all stage types that accept platform overrides.

## Variable interpolation

`${VAR}` in URLs and paths is resolved in this order (higher overrides lower):

1. **Entry-level variables** — `variables` inline table or `version` shorthand
   on `[[vendor]]`/`[[binary]]`/`[[resource]]`/`[[fetch]]` entries
2. **Stage-level variables** — `vars` inline table on the stage
3. **System defaults** — `PLATFORM`, `ARCH`, `CPU` (mapped from ARCH),
   `OS`, `EXE_SUFFIX`

Matching is case-insensitive: `version = "0.14.0"` resolves `${VERSION}`.

### Entry-level variables

Variables are declared on each entry using an inline table:

```toml
[[vendor]]
name = "hugo"
url = "https://github.com/.../v${VERSION}/hugo_${VERSION}_${PLATFORM}-${CPU}.tar.gz"
variables = { version = "0.158.0" }
```

For entries with only a `version`, the flat `version` key is a shorthand:

```toml
[[vendor]]
name = "hugo"
url = "https://github.com/.../v${VERSION}/hugo_${VERSION}_${PLATFORM}-${CPU}.tar.gz"
version = "0.158.0"
```

### Stage-level vars

```toml
[[stages]]
name = "sdl"
type = "vendor"
url = "https://example.com/sdl-${VERSION}.tar.gz"
vars = { version = "2.30" }
```

## Root-level download entries

Four root-level arrays merge download entries into matching stages:

| Array | Merges into | Default destination |
| ----- | ----------- | ------------------- |
| `[[vendor]]` | `type = "vendor"` stages | `root://vendor/` |
| `[[resource]]` | `type = "resource"` stages | `root://resources/` |
| `[[binary]]` | `type = "fetch"` with `fetch-type = "binary"` | `cache://bin/` |
| `[[fetch]]` | `type = "fetch"` matching by `fetch-type` | varies by `fetch-type` |

Each entry supports:

| Field | Description |
| ----- | ----------- |
| `name` | Entry identifier |
| `url` | Download URL (supports `${VAR}`) |
| `dest` | Destination path (defaults per type above) |
| `sha256` | Verification hash |
| `extract` | Whether to extract archives (auto-detected for .tar.gz/.zip) |
| `create_directory` | Create the dest directory if missing |
| `output_name` | Rename the downloaded file |
| `version` | Shorthand for setting `version` in `variables` |
| `variables` | Per-entry variable inline table |
| `include` | Glob patterns to filter extracted files (string or array) |
| `exclude` | Glob patterns to exclude from extraction (string or array) |

Example:

```toml
[[vendor]]
name = "tomlpp"
url = "https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp"
sha256 = "6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e"
dest = "root://vendor/"
create_directory = true
```

### Root-level fetch entries

`[[fetch]]` entries carry their own `fetch-type`:

```toml
[[fetch]]
name = "shellcheck"
url = "https://example.com/shellcheck-${VERSION}.tar.gz"
fetch-type = "binary"

[[fetch]]
name = "sdl"
url = "https://example.com/sdl-${VERSION}.tar.gz"
fetch-type = "vendor"
variables = { version = "2.30" }
```

They merge into `type = "fetch"` stages where `fetch-type` matches.

## Install config

The root-level `[install]` table auto-generates `install` and `uninstall`
stages:

```toml
[install]
depends = ["build"]
dir = "/opt/myapp"
symlink = true
artifacts = [
    { source = "root://bin/myapp", dest = "myapp" },
]
```

| Field | Default | Description |
| ----- | ------- | ----------- |
| `depends` | — | Stage dependencies |
| `dir` | Platform-dependent | Install prefix (Unix: `/usr/local/bin`, Windows: `C:/Program Files/<project>`) |
| `artifacts` | — (required) | Array of `{ source, dest }` — source is project/cache path, dest is relative to install dir |
| `symlink` | `false` | Create `/usr/local/bin/<project>` symlink (Unix only, skipped when dir == `/usr/local/bin`) |

Platform overrides:

```toml
[install.platform.darwin]
dir = "/opt/myapp"
```

Each artifact can set `binary = true` to auto-append `.exe` on Windows, or
`userdir = true` to install to the user's home directory instead of the
system prefix.

## Package stage artifacts

Artifacts in package and install stages support the `binary` field:

```toml
artifacts = [
    { source = "root://bin/myapp", dest = "myapp", binary = true },
]
```

When `binary = true`, the source and dest paths get `.exe` appended on
Windows automatically.
