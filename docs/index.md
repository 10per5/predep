---
title: predep
type: docs
---

# predep — Stage Processing Engine

<br />

[![GitHub](https://img.shields.io/badge/GitHub-10per5/predoc-181717?style=for-the-badge\&logo=github)](https://github.com/10per5/predoc)

***

A cross-platform stage processor: reads a declarative TOML manifest, resolves
stages and their dependencies in order, and produces artifacts.

* [Stage types & usage](stages/) — reference for all stage types with examples

* [Config format](config.md) — manifests, includes, namespacing, variable interpolation, platform overrides

* [Resolution order](data-model.md) — how stages, variables, and platform overrides are resolved

* [Architecture](architecture.md) — application flow, dependencies, cross-platform support

* [Security model](security.md) — stage risk levels, injection prevention, runtime guards
