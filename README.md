# predep — Stage Processing Engine

> **Experimental** — work in progress. Security hardening, sandboxing, and
> audit capabilities are planned for future releases.

A cross-platform stage processor: reads a declarative TOML manifest, resolves
stages and their dependencies in order, and produces artifacts. Used by the
[predoc](https://github.com/10per5/predoc) project.

## Quick Start

```
predep [options] [<stage>]
```

| Command          | What it does                                   |
| ---------------- | ---------------------------------------------- |
| `predep`         | Resolve the stage defined as `main` in config  |
| `predep <stage>` | Resolve a single stage and its transitive deps |
| `predep --list`  | List available stages from all manifests       |

Options: `--debug`, `--platform <name>`, `--config <path>`, `--os <os>`, `--privileged`, `--help`

## Resolution Order

1. Check if stage outputs already exist → skip
2. Resolve all transitive dependencies first (DAG with cycle detection)
3. Execute the stage action (download / run / docker / package)
4. Verify declared outputs

## Stage Types

| Type                             | Behavior                                                              |
| -------------------------------- | --------------------------------------------------------------------- |
| `vendor` / `binary` / `resource` | Download + SHA256 verify + extract, scoped to root/cache paths        |
| `run`                            | Execute shell commands (platform overrides via `[platform.xxx]`)      |
| `docker`                         | Build image, create container, copy out target                        |
| `group`                          | No-op — checks all outputs exist, groups stages into a single command |
| `package`                        | Copy artifacts into dist/ and create platform archive (tar.gz / zip)  |

## Manifests

Each subproject declares its own `predep.toml`. The root manifest uses
`[[include]]` to compose them — includes are namespaced (default: directory
name), stages referenceable as `namespace::name`.

URLs and paths support `${VAR}` interpolation from entry-level keys,
stage `[vars]` tables, or system defaults (`PLATFORM`, `ARCH`, `OS`, etc.).

See [docs/config.md](docs/config.md) for the full config format.

## Build

```bash
# Native
premake5 gmake && make config=release -j$(nproc)

# Self-hosted
predep --config predep.toml predep-build

# Docker (Linux / Windows cross)
docker build -f images/linux.Dockerfile -t predep-builder .
```

Runtime deps: libcurl, OpenSSL, libpthread (Linux only).

## Project docs

- [Architecture & design](docs/architecture.md) — engine design, cache, path resolution
- [Stage types & usage](docs/stages.md) — detailed stage type reference with examples
- [Config format](docs/config.md) — manifests, includes, variable interpolation, overrides
