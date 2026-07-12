---
title: Resolution Order
weight: 30
---

# Resolution Order

When a stage runs, the engine walks a dependency graph and executes each
stage in order. Here is how resolution works at each level.

## Stage resolution

1. Resolve all transitive dependencies first (DAG walk with cycle detection)
2. Resolve the working directory from `build_context`
3. Merge platform overrides into stage defaults
4. Run the stage action (download, execute commands, build Docker image, etc.)

> **Planned:** An output-based skip-if-built check and post-execution output
> verification are on the roadmap. Currently every execution stage re-runs
> on every call. The `outputs` field in stage definitions is reserved for
> this future feature and already used by the premake5 strip step.

## Variable resolution

`${VAR}` in URLs and paths is resolved in this order (higher overrides lower):

1. **Entry-level** — `variables` inline table or `version` shorthand on
   download entries
2. **Stage-level** — `vars` inline table on the stage
3. **System defaults** — `PLATFORM`, `ARCH`, `CPU` (mapped from ARCH),
   `OS`, `EXE_SUFFIX`

Matching is case-insensitive.

## Platform override merging

Each action starts with its **defaults**, then merges fields from the
platform override for the current OS. Only the fields you specify differ
from the defaults — platform overrides are partial.

`build_context` resolves in this order (higher priority first):

1. Platform override `build_context` (if set)
2. Stage default `build_context`

## Path resolution

Two prefixes abstract filesystem layout:

- `root://` — resolves to the project root (configurable via `PREDEP_DIR`)
- `cache://` — resolves to the platform cache directory

All paths in config files use these prefixes. The engine translates them
to absolute filesystem paths at resolution time.
