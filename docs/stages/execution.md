---
title: Execution
weight: 12
---

# Execution

Stage types that run commands or build things.

## Run

Executes shell commands. Supports platform-specific overrides:

```toml
[[stages]]
name = "build-app"
type = "run"
commands = ["make -j$(nproc)"]
depends = ["sdl"]
platform.windows.commands = ["msbuild app.sln /p:Configuration=Release"]
```

Falls back to the default `commands` when no platform override matches.

### When to use
- Compiling native code
- Running code generation tools
- Any build step that involves shell commands

## Binary

Executes a known binary with arguments — safer than `run` since it uses
`execvp` (no shell). Checks `cache://bin` then system PATH.

```toml
[[stages]]
name = "hugo-build"
type = "binary"
binary = "hugo"
params = { contentDir = "../content", destination = "build" }
```

Supports platform overrides for `params`, `args`, and `build_context`.
Appends `.exe` on Windows automatically.

### When to use
- Running a specific tool that should be vendored or in PATH
- Avoiding shell injection risks of `run` stages
- Getting clear errors if the binary is not installed

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
- When the host system lacks required build dependencies
