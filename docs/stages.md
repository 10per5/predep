# Stage types & usage

Each stage in `predep.toml` has a `type` that determines what action it
performs. Stages declare their own dependencies, outputs, and variables.

## Vendor / Binary / Resource

Download stages — fetch a remote archive or file, verify its SHA256, and
extract to a standard location:

| Type | Default destination |
| ---- | ------------------ |
| `vendor` | `root://vendor/` |
| `binary` | `cache://bin/` |
| `resource` | `root://resources/` |

```toml
[[stages]]
name = "sdl"
type = "vendor"
url = "https://example.com/sdl-${VERSION}.tar.gz"
version = "2.30"
dest = "root://vendor/"
sha256 = "abc123..."
```

### When to use
- **vendor** — third-party libraries checked into the project tree
- **binary** — CLI tools needed at build time (e.g., Hugo, Bun) cached per-user
- **resource** — static assets bundled with the project (icons, templates)

## Run

Executes shell commands. Supports platform-specific overrides:

```toml
[[stages]]
name = "build-app"
type = "run"
commands = ["make -j$(nproc)"]
depends = ["sdl"]

[platform.windows]
commands = ["msbuild app.sln /p:Configuration=Release"]
```

Falls back to top-level `commands` when no platform override matches.

### When to use
- Compiling native code
- Running code generation tools
- Any build step that involves shell commands

## Docker

Builds a Docker image, creates a container, and copies out the target path:

```toml
[[stages]]
name = "cross-linux"
type = "docker"
recipe = "images/linux.Dockerfile"
target = "/src/out/app"
dest = "root://bin/"
depends = ["vendor-sdl"]
```

Supports platform-specific recipe/target/dest overrides.

### When to use
- Cross-compilation (e.g., building Windows binaries from Linux)
- Reproducible builds with controlled toolchain versions
- When host system lacks required build dependencies

## Group

A no-op stage — resolves instantly and only checks that all outputs exist.
Useful for naming a collection of stages as a single command:

```toml
[[stages]]
name = "all"
type = "group"
depends = ["build-app", "build-docs", "bundle-assets"]
```

### When to use
- Creating a top-level alias for multiple stages
- Organising stages into logical groups without adding behaviour
- Defining the `main` stage target

## Package

Copies artifacts into `dist/` and creates a compressed archive:

```toml
[[stages]]
name = "package"
type = "package"
bundle = "myapp"
depends = ["all"]
artifacts = [
    { source = "root://bin/myapp", dest = "bin/" },
    { source = "root://public/", dest = "public" },
]
```

Produces `myapp.tar.gz` (Unix) or `myapp.zip` (Windows).

### When to use
- Assembling a release distribution
- Creating deployable archives with a consistent layout
- Bundling multiple build outputs into a single artifact

## Build context

Every stage can set `build_context` to control the working directory:

| Value | Behaviour |
| ----- | --------- |
| `"self"` (default) | Stage's config directory |
| `"parent"` | Parent of the config directory |
| relative path | Resolved against the config directory |

Custom relative paths trigger a confirmation prompt.
