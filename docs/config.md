# Config format

predep uses TOML manifests. Each project declares stages in `predep.toml`.
The root manifest composes subproject manifests with `[[include]]`.

## Root manifest

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

## Include & namespacing

`[[include]]` imports stages from other manifests:

| Field | Description |
| ----- | ----------- |
| `path` | Path to included manifest (relative to including file) |
| `namespace` | Namespace prefix (default: including file's directory name) |
| `only` | Optional — only import specific stages |

Stages from included files are referenceable as `namespace::name`.
Internal dependencies within a file are automatically rewritten with the
prefixed names.

```toml
[[include]]
path = "subdir/predep.toml"
namespace = "custom"
only = ["stage-a", "stage-b"]
```

## Variable interpolation

`${VAR}` in URLs and paths is resolved in this order (higher overrides lower):

1. **Entry-level variables** — `[variables]` table or `version` shorthand on
   `[[vendor]]`/`[[binary]]`/`[[resource]]` entries
2. **Stage-level variables** — `[vars]` table on the stage
3. **System defaults** — `PLATFORM`, `ARCH`, `CPU` (mapped from ARCH),
   `OS`, `EXE_SUFFIX`

```toml
[[vendor]]
name = "hugo"
url = "https://github.com/.../v${VERSION}/hugo_${VERSION}_${PLATFORM}-${CPU}.tar.gz"
version = "0.158.0"
```

Matching is case-insensitive: `version = "0.14.0"` resolves `${VERSION}`.

### Stage-level vars

```toml
[[stages]]
name = "sdl"
type = "vendor"
url = "https://example.com/sdl-${VERSION}.tar.gz"

[vars]
version = "2.30"
```

## Platform overrides

Any stage can specify platform-specific overrides via `[platform.<os>]`:

```toml
commands = ["make -j$(nproc)"]

[platform.windows]
commands = ["msbuild myapp.sln /p:Configuration=Release"]

[platform.darwin]
commands = ["xcodebuild -project myapp.xcodeproj"]
```

Supported platforms: `linux`, `darwin`, `windows`.

## Package stage config

The `package` type copies artifacts and creates an archive:

```toml
[[stages]]
name = "package"
type = "package"
bundle = "myapp"
artifacts = [
    { source = "root://bin/myapp", dest = "bin/" },
    { source = "root://public/", dest = "public" },
]
```

- `bundle` — archive filename (default: `release`)
- `artifacts` — array of `{ source, dest }` maps

Outputs `myapp.tar.gz` (Unix) or `myapp.zip` (Windows).

## Docker stage config

```toml
[[stages]]
name = "cross-linux"
type = "docker"
recipe = "images/linux.Dockerfile"
target = "/src/out/app"
dest = "root://bin/"

[platform.windows]
recipe = "images/win32.Dockerfile"
target = "/src/out/app.exe"
dest = "root://bin/"
```
