---
title: Other
weight: 15
---

# Other

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

## Build context

Every stage can set `build_context` to control the working directory:

| Value | Behaviour |
| ----- | --------- |
| `"self"` (default) | Stage's config directory |
| `"parent"` | Parent of the config directory |
| relative path | Resolved against the config directory |

Custom relative paths trigger a confirmation prompt unless `--privileged`
is used.
