---
title: Stage Types & Usage
weight: 10
bookFlatSection: true
---

# Stage types & usage

Each stage in `predep.toml` has a `type` that determines what action it
performs. Stages declare their own dependencies, outputs, and variables.

- [Dependencies](dependencies) — vendor, fetch, resource (downloading stuff)
- [Execution](execution) — run, binary, docker (running commands)
- [Building](building) — premake5 (build system integration)
- [Bundling & install](bundling) — package, install, uninstall (distribution)
- [Other](other) — group, disabled, build context
