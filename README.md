# predep — Stage Processing Engine

A cross-platform stage processor: reads a declarative manifest, resolves
stages and their dependencies in order, and produces artifacts.

## Usage

```
predep [options] [<stage>]
```

| Command          | What it does                                   |
| ---------------- | ---------------------------------------------- |
| `predep`         | Resolve the stage defined as `main` in config  |
| `predep <stage>` | Resolve a single stage and its transitive deps |
| `predep --list`  | List available stages from all manifests       |

Options: `--debug`, `--platform <name>`, `--config <path>`, `--os <os>`, `--list`, `--help`

## Manifests

Each subproject declares its own `predep.toml`. The root manifest uses
`[[include]]` to compose them:

```toml
main = "package"

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

Includes are namespaced by directory name by default, or explicitly via
`namespace`. Stages from included files are accessible as `namespace::name`.

### Stage Types

| Type                             | Behavior                                                                                                      |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `download`                       | PATH → cache → download + SHA verify + extract. Supports `[[vendor]]`/`[[binary]]`/`[[resource]]` sub-entries |
| `run`                            | Execute shell `commands` list                                                                                 |
| `vendor` / `binary` / `resource` | Shorthand for download stages scoped to root/cache paths                                                      |
| `docker`                         | Auto lifecycle: build, create, cp, rm. Fields: `recipe`, `target`, `dest`                                     |
| `group`                          | No-op (deps handle the work); checks all outputs exist                                                        |
| `package`                        | Copy artifacts into dist/ and create platform archive                                                         |
| `run` (w/ platform overrides)    | Platform-specific commands via `[platform.xxx]` table                                                         |

### Package Stage

```toml
[[stages]]
name = "package"
type = "package"
bundle = "myapp"              # archive filename (default: "release")
depends = ["build"]
artifacts = [
    { source = "root://bin/myapp", dest = "bin/" },
    { source = "root://public/", dest = "public" },
]
```

Produces `myapp.tar.gz` (Unix) or `myapp.zip` (Windows) containing the
assembled artifacts.

### Variable Interpolation

URLs and paths support `${VAR}` interpolation. Variables come from:

1. Entry-level flat string keys (`version = "1.0"`)
2. Stage-level flat string keys
3. System defaults: `PLATFORM`, `ARCH`, `CPU` (mapped from ARCH),
   `OS`, `EXE_SUFFIX`

```toml
[[binary]]
name = "hugo"
url = "https://github.com/.../v${VERSION}/hugo_${VERSION}_${PLATFORM}-${CPU}.tar.gz"
extract = true
version = "0.158.0"
```

### Platform Overrides

Any stage can specify platform-specific overrides:

```toml
commands = ["make -j$(nproc)"]

[platform.windows]
commands = ["msbuild myapp.sln /p:Configuration=Release"]
```

## Resolution Order

1. Check if outputs exist → skip if already resolved
2. Resolve all deps first (dependency tree, cycle detection)
3. Execute the stage action (download / run / docker)
4. Verify outputs

## Cache

Cache dirs are platform-specific, overridable via `PREDEP_CACHE` env var.

## Build

### Native (via premake5)

```bash
premake5 gmake && make config=release -j$(nproc)
```

Runtime deps: libcurl, OpenSSL, libpthread (Linux only).
Build deps: premake5, g++ (C++23).

### Self-hosting

```bash
predep --config predep.toml predep-build
```

### Docker

Dockerfiles for a standalone first build are in `images/`. They download
vendor headers (tomlpp, CLI11) and system deps automatically — no premake5 or
libraries needed on the host.

```bash
# Linux native build
docker build -f images/linux.Dockerfile -t predep-builder .
docker create --name predep-tmp predep-builder
docker cp predep-tmp:/src/predep/bin/predep ./bin
docker rm predep-tmp

# Windows cross-build (from Linux)
docker build -f images/win32.Dockerfile -t predep-cross .
docker create --name predep-tmp predep-cross
docker cp predep-tmp:/src/predep/bin/predep.exe ./bin
docker cp predep-tmp:/src/predep/bin/libcurl-x64.dll ./bin
docker rm predep-tmp
# libcurl-x64.dll must be kept alongside predep.exe at runtime
```
