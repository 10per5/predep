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
