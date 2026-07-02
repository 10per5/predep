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

## Build context

Every stage can set `build_context` to control the working directory:

| Value | Behaviour |
| ----- | --------- |
| `"self"` (default) | Stage's config directory |
| `"parent"` | Parent of the config directory |
| relative path | Resolved against the config directory |

Custom relative paths trigger a confirmation prompt unless `--privileged`
is used.
