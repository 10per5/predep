# predep — Stage Processing Engine

<br />

[![GitHub](https://img.shields.io/badge/GitHub-10per5/predoc-181717?style=for-the-badge\&logo=github)](https://github.com/10per5/predoc)

***

A cross-platform stage processor: reads a declarative TOML manifest, resolves
stages and their dependencies in order, and produces artifacts.

* [Architecture](architecture.md) — application flow, component design, cross-platform support

* [Stage types & usage](stages.md) — reference for vendor, run, docker, group, package with examples

* [Config format](config.md) — manifests, includes, namespacing, variable interpolation, overrides

* [Security model](security.md) — stage risk levels, injection prevention, runtime guards, roadmap
