# predep — Stage Processing Engine

> **Experimental** — work in progress.

A cross-platform stage processor: reads a declarative TOML manifest, resolves
stages and their dependencies in order, and produces artifacts. Used by
[predoc](https://github.com/10per5/predoc).

```
predep [options] [<stage>]
```

| Command | What it does |
| ------- | ------------ |
| `predep` | Resolve the stage defined as `main` in config |
| `predep <stage>` | Resolve a single stage and its transitive deps |
| `predep --list` | List available stages from all manifests |

Options: `--debug`, `--platform <name>`, `--config <path>`, `--os <os>`, `--privileged`, `--help`

See [docs/](docs/) for full documentation.

## Quick build

```bash
# Using the latest binary from GitHub (self-hosting)
predep --config predep.toml build

# Or build from source
premake5 gmake && make config=release -j$(nproc)
```
