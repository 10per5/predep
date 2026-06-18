# Architecture

predep is a stage-processing engine. It reads a declarative TOML manifest,
resolves stages and their dependencies, and produces artifacts. **It is not
tied to any specific project** — the stages in `predep.toml` define what it
does.

---

## Application flow

```
CLI args → config discovery → engine → resolver → action dispatch → result
```

1. **Entry point** parses CLI flags (stage name, platform override, debug mode,
   config path, force flag). Discovers the project root by walking up from the
   working directory looking for `predep.toml`.

2. A **runtime context** is assembled with the resolved root path, platform
   cache directory, target OS, architecture, logger strategy, and prompter
   strategy (interactive / force / null).

3. The **engine** loads the manifest — this means parsing the root TOML,
   recursing into `[[include]]` entries, merging stages under their namespaces,
   and interpolating `${VAR}` references throughout URLs and paths.

4. The **resolver** takes the stage name requested (or `main` stage by default)
   and walks the dependency graph. Cycles are detected and rejected. Each
   stage is dispatched to its registered **action handler**.

5. Each action handler checks whether outputs already exist (skip if so),
   executes its work (download a file, run shell commands, build a Docker
   image, assemble a package), and verifies that declared outputs were
   produced.

---

## Core components

### Engine (public facade)

The single entry point for the application. Owns the config loader and
resolver internally. Provides `load_toml()` to ingest a manifest and
`resolve()` / `resolve_all()` to process stages. Clients interact only with
the engine.

### Config loader

Parses TOML manifests. Handles `[[include]]` directives — each include is
namespaced (default: the including directory's name), and stages are
referenceable as `namespace::name`. Performs variable interpolation on URLs
and paths, resolving `${VAR}` from entry-level keys, stage-level `[vars]`
tables, or system defaults (platform, arch, OS, etc.).

### Resolver (DAG traversal engine)

Maintains two maps: a registry of all stage descriptors keyed by name, and a
registry of action handlers keyed by stage type. Given a stage name, it walks
the dependency graph depth-first, tracking visiting/resolved sets for cycle
detection. Each stage is dispatched to the appropriate action handler.

### Stage descriptor (universal stage model)

Every stage, regardless of type, is represented by a single descriptor struct
containing: name, type, description, dependency list, output list, build
context, config directory, platform-specific overrides, a variables table, and
a polymorphic pointer to type-specific data. This separation means the resolver
and engine work with a uniform interface while each action type carries only
the fields it needs.

### Action handlers (one per stage type)

Each handler implements two operations: check whether the stage is already
resolved (outputs exist and are valid), and execute the stage. Handlers share
static helpers for resolving the working directory from build context and for
prompting the user to confirm out-of-tree execution.

- **Download action** — downloads remote files with SHA256 verification and
  retry logic, extracts tar.gz/zip archives to the appropriate root or cache
  path. Supports vendor, binary, and resource variants with different default
  destinations.
- **Run action** — executes shell commands. Supports platform-specific command
  overrides so the same stage can run `make` on Linux and `msbuild` on Windows.
- **Docker action** — builds a Docker image, creates a container, copies the
  target path out, and cleans up the container. Supports platform-specific
  recipe/target/dest overrides for cross-compilation workflows.
- **Group action** — a no-op that resolves instantly. Only checks that
  declared outputs exist. Useful for giving a single name to a collection of
  stages.
- **Package action** — copies artifacts from source paths into a dist
  directory and creates a compressed archive (tar.gz on Unix, zip on Windows).

### Runtime context

A struct threaded through the entire resolution chain. Carries the project
root, cache directory, target OS and platform identifiers, concurrency limit,
and pointers to the active logger and prompter. Also provides `resolve_path()`
which translates `root://` and `cache://` prefixes to absolute filesystem paths.

### Logger (strategy pattern)

Abstract interface with four implementations:
- **ColorLogger** — terminal output with ANSI colour codes
- **MonochromeLogger** — plain terminal output
- **MinifiedLogger** — short, machine-friendly output
- **NullLogger** — suppresses all output

Selected by the `--format` CLI flag via a factory function.

### Prompter (strategy pattern)

Abstract interface for user confirmation with three implementations:
- **InteractivePrompter** — checks for a TTY, displays a formatted warning box
  on stderr, reads yes/no from stdin
- **ForcePrompter** — auto-accepts (`--force` flag)
- **NullPrompter** — auto-rejects (safe default when no TTY is available)

### System abstraction layer (namespaces)

Three namespaces provide OS-independent access to common operations:

- **platform** — file existence checks, OS and architecture detection, cache
  directory lookup, executable path resolution, file hashing
- **download** — HTTP GET with progress callback, SHA256 verification with
  retry logic for transient failures
- **extract** — archive extraction supporting tar.gz and zip formats
- **process** — subprocess execution, stdout capture, stderr capture, shell
  command detection (cmd.exe vs /bin/sh)

---

## How components interact

```
CLI args → engine.load_toml() → config_loader parses + interpolates
                                → populates stage descriptor map

engine.resolve() → resolver.walk(stage)
                     ↓
              for each dependency:
                resolver.walk(dep) ← recursive, cycle-checked
                     ↓
              find action handler by stage type
                     ↓
              action.resolve(stage_desc, runtime)
                     ↓
              check outputs → skip if done
              execute (download / run / docker / package)
              verify outputs → error if missing
```

The `runtime` context flows through every call, providing path resolution,
logging, and prompting. The `stage_desc` is the single shared data contract
between the config layer, the resolver, and all action handlers.

---

## Dependencies

### Build-time
- **premake5** — meta-build system (generates Makefiles / VS solutions)
- **C++23 compiler** — g++ (Linux), clang (macOS), MSVC (Windows)
- Docker images in `images/` allow building without premake5 or system libs

### Runtime (linked)
- **libcurl** — HTTP downloads (Unix: system curl, Windows: libcurl.dll)
- **OpenSSL / libcrypto** — SHA256 verification (WinSSL on Windows)
- **libpthread** — threading support (Linux only)

### Vendored (in-tree)
- **toml++** — TOML parsing
- **CLI11** — CLI argument parsing

---

## Cross-platform support

### Build
- premake5 generates platform-native build files: Makefiles for Linux/macOS,
  Visual Studio solutions for Windows
- Docker-based cross-compilation: Linux → Windows (MinGW cross toolchain)
- Single codebase with platform-conditional linkage (`filter("system:...")`)
- All file operations use C++17 `std::filesystem` for path normalization

### Runtime
- `platform::os()` detects the host OS at startup
- Cache directory is resolved per-platform convention
- Shell command detection: `cmd /c` on Windows, `/bin/sh -c` on Unix
- Archive format: tar.gz on Unix, zip on Windows (both can be read)
- Forward slashes in config files are accepted on all platforms (Windows
  `cmd.exe` handles them, and `std::filesystem` normalizes at the C++ layer)

### Config portability
- TOML manifests are platform-agnostic
- `[platform.<os>]` tables provide per-OS command/recipe overrides within the
  same manifest, avoiding platform-specific config files
- `${OS}`, `${PLATFORM}`, `${ARCH}` variables automatically adapt URLs and
  paths to the target platform
