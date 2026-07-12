---
title: Other
weight: 15
---

# Other

## Group

A no-op stage — resolves instantly and does nothing. Useful for naming a
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

## Disabled

When no `type` is specified, defaults to `"disabled"`. The stage prints a
message that it is not implemented yet and exits cleanly.

```toml
[[stages]]
name = "placeholder"
```

### When to use
- Placeholder stages for work-in-progress
- Skipping a stage without deleting its definition

## Clean

Removes artifacts produced by target stages. Targets opt in via `clean = true`
and can specify extra `clean_paths`. The clean stage itself accepts a `targets`
array (stages whose artifacts to remove — not resolved as build dependencies)
and a `paths` array for entries like `node_modules`.

```toml
[[stages]]
name = "sdl"
type = "vendor"
url = "..."
clean = true                      # opt in for collection
clean_paths = ["root://vendor/sdl-cache"]

[[stages]]
name = "cleanup"
type = "clean"
targets = ["sdl"]
paths = ["root://node_modules"]   # extra paths beyond target collection
```

Collected paths from each target (when `clean = true`):
- `outputs` (all buildable types)
- `clean_paths` (all buildable types)
- `target` (premake5)
- `dest` (docker)
- artifact sources (package)

## Copy

Distributes local asset files from a source location into one or more
destination paths. It validates that each source exists (failing the stage by
default when one is missing), then `cp -f` each source into every destination,
creating parent directories as needed. This replaces ad-hoc `init-assets`
shell scripts that fan one image out across several project subtrees.

```toml
[[stages]]
name = "init-assets"
type = "copy"
source_dir = "root://images"        # base dir for relative `source` paths
fail_if_missing = true              # default; set false to skip missing sources
files = [
  { source = "favicon.ico",   dests = [
      "root://editor/public/favicon.ico",
      "root://hugo-view/static/favicon.ico",
      "root://gui/bin/icon.ico",
  ] },
  { source = "inb4doc-64.png", dests = [
      "root://hugo-view/static/favicon.png",
      "root://gui/bin/icon.png",
  ] },
]
```

### Path confinement (Layer 6)

All `source` and `dest` paths are confined to the **project root** (`root://`):

- Paths **must** resolve within `root://`. Unprefixed paths are anchored to the
  project root (or `source_dir` for sources) and are accepted only when they
  stay within it.
- `cache://` is **never** allowed for copy stages — neither for sources nor
  destinations.
- Paths that escape the project root are rejected.
- This confinement is a hard config error and **cannot** be bypassed with
  `--privileged` (unlike fetch/install which may target cache or system dirs).

### Fields

| Field | Default | Description |
| ----- | ------- | ----------- |
| `source_dir` | — | Optional base directory (must be `root://`); relative `source` paths resolve against it. |
| `fail_if_missing` | `true` | When `true`, a missing source fails the stage; when `false`, it is skipped with a warning. |
| `files` | — (required) | Array of `{ source, dests }` tables. Each `source` is copied to every path in `dests`. |

The stage is idempotent: it is considered already resolved when every
destination exists with content matching its source, so re-running a resolved
stage is a no-op.

### When to use
- Fanning a small set of icons/images out into multiple consumer subtrees
- Populating git-ignored directories (e.g. `public/`, `bin/`) from a tracked `images/`
- Any "copy these local files into place" prep step that does not need shell logic

## Build context

Every stage can set `build_context` to control the working directory:

| Value | Behaviour |
| ----- | --------- |
| `"self"` (default) | Stage's config directory |
| `"parent"` | Parent of the config directory |
| relative path | Resolved against the config directory |

Custom relative paths trigger a confirmation prompt unless `--privileged`
is used.
