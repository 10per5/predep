# predep — Stage Processing Engine

> **Experimental** — work in progress.

A cross-platform **stage processor**: reads a declarative TOML manifest
(`predep.toml`), resolves stages and their dependencies in dependency order, and
produces artifacts (vendored libraries, built binaries, packages, installs). It
is project-agnostic — the stages you declare in `predep.toml` decide what it
does. Used by [predoc](https://github.com/10per5/predoc).

```
predep [options] [<stage>]
```

| Command | What it does |
| ------- | ------------ |
| `predep` | Resolve the stage named by `main` in config |
| `predep <stage>` | Resolve a single stage and its transitive deps |
| `predep --list` | List available stages from all manifests |

Options: `--debug`, `--platform <name>`, `--config <path>`, `--os <os>`,
`--privileged`, `--version`, `--help`.

---

## Usage guidelines

### Install

Get the latest prebuilt binary (self-hosting) or build from source:

```bash
# Latest binary from GitHub releases
curl -fsSL "https://github.com/10per5/predep/releases/latest/download/predep-linux-x86_64.tar.gz" \
  | tar -xz && install -m 0755 predep /usr/local/bin/predep

# Or build from source (premake5 + make)
premake5 gmake && make config=release -j$(nproc)
```

### A minimal manifest

```toml
main = "build"
project = "myapp"

[[stages]]
name = "vendor"
type = "vendor"

[[vendor]]
name = "tomlpp"
url  = "https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp"
sha256 = "6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e"
dest = "root://vendor/"
create_directory = true

[[stages]]
name = "build"
type = "premake5"
depends = ["vendor"]
config = "release"
outputs = ["root://bin/myapp"]
```

Then run `predep` (resolves `main` = `build`, which pulls `vendor` first), or
`predep vendor` to fetch deps only.

### Core concepts

- **Stages** — units of work (`vendor`, `fetch`, `run`, `binary`, `docker`,
  `premake5`, `cmake`, `package`, `install`, `uninstall`, `clean`, `group`,
  `copy`). Each declares its own `depends`, `outputs`, and variables.
- **`root://`** = the directory containing `predep.toml`; **`cache://`** = the
  platform cache dir. All paths in config use these prefixes.
- **Dependency resolution** — `depends` wires a DAG; predep resolves transitive
  deps in order. `[[include]]` composes sub-manifests under namespaces
  (`namespace::name`).
- **Idempotency** — downloads verify `sha256` (or a `.predepgit` ref marker for
  git vendors); `cmake`/`premake5` rebuild only when inputs change.
- **Variable interpolation** — `${VAR}` in URLs/paths resolves from per-entry
  `version`/`variables`, stage-level `vars`, then system defaults
  (`PLATFORM`, `ARCH`, `CPU`, `OS`, `EXE_SUFFIX`).

See [`docs/`](docs/) for the full reference and [`skills/`](skills/) for
task-oriented recipes (below).

---

## Documentation

### Reference (`docs/`)

- [Config format](docs/config.md) — manifests, includes, namespacing, variables, platform overrides
- [Stage types](docs/stages/) — per-stage-type fields and examples
- [Resolution order](docs/data-model.md) — how stages/variables/platform overrides resolve
- [Architecture](docs/architecture.md) — application flow, dependencies, cross-platform support
- [Security model](docs/security/security.md) — stage risk levels, injection prevention, runtime guards

### Skills (`skills/`) — for agentic workflows

These recipe-style guides are written for AI agents (and humans) automating
common predep tasks. If you arrived here via an automated workflow (WGET/curl of
this repo), start with the relevant skill:

| Skill | When to use |
| ----- | ----------- |
| [`skills/vendor-libraries.md`](skills/vendor-libraries.md) | Vendor a library and build it (archive or git + `cmake`/`premake5` via `builder`) |
| [`skills/vendor-single-file.md`](skills/vendor-single-file.md) | Vendor a single header/file pinned with a `sha256` |
| [`skills/vendor-git-ref.md`](skills/vendor-git-ref.md) | Vendor source from a GitHub branch / tag / commit SHA |
| [`skills/dependency-trees.md`](skills/dependency-trees.md) | Express `depends`, `[[include]]` namespacing, and the `builder` auto-link |
| [`skills/building-stages.md`](skills/building-stages.md) | Build with `premake5` (preferred on vanilla), `cmake`, or `make` |
| [`skills/predep-dockerfile.md`](skills/predep-dockerfile.md) | Use predep inside a Dockerfile to vendor + build |

> **Agent note:** when vendoring dependencies, prefer pinning to an exact tag or
> commit SHA and recording `sha256`; track reproducible builds. Prefer `premake5`
> on single-project C++ repos and `cmake` for CMake-based deps.
