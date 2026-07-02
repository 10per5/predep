---
title: Sandboxing
weight: 55
---

# Sandbox Plan

`type = "binary"` stages today execute with the full privileges of the user —
full filesystem access, network access, all capabilities, and inherited
environment. The `--sandbox` flag and its associated config layer will confine
binary execution to only what it needs.

## Architecture

A pluggable backend system, detected at runtime by availability:

| Backend    | Requires            | Isolates                     | Since               |
|------------|---------------------|------------------------------|---------------------|
| `bwrap`    | `bwrap` on PATH     | Mount, PID, net, IPC, UTS, cgroup, user | Linux 3.8 (unshare) |
| `landlock` | Kernel ≥ 5.13       | Filesystem only              | Linux 5.13          |
| `seccomp`  | Kernel ≥ 3.17       | Syscall filtering            | Linux 3.17          |

Detection order: `bwrap` → `landlock` → no sandbox (fallback with warning).
Other platforms: no sandbox available, `--sandbox` warns and skips.

> Windows sandboxing (AppContainer) is planned separately — see `roadmap/sandbox-win32.md`.

## Privilege Dimensions

Each dimension has an enum of permitted values, defaulting to the most
restrictive option:

### Network

| Setting          | bwrap              | landlock | seccomp          |
|------------------|--------------------|----------|------------------|
| `network = "none"`   | `--unshare-net`    | N/A      | Block `socket`, `connect`, `bind`, `sendto` |
| `network = "loop"`   | `--share-net` + iptables loopback | N/A | Restrict `connect`/`bind` to 127.0.0.1/::1 |
| `network = "full"`   | `--share-net`      | N/A      | Allow all network syscalls |

### Capabilities

| Setting        | bwrap               | landlock | seccomp                  |
|----------------|----------------------|----------|--------------------------|
| `caps = "none"`   | `--cap-drop ALL`     | N/A      | Drop all cap syscalls    |
| `caps = "keep"`   | Omit `--cap-drop`    | N/A      | Allow cap syscalls       |

`caps = "none"` is the default and strongly recommended. Only stages that
need `CAP_NET_BIND_SERVICE`, `CAP_SYS_PTRACE`, etc. should opt in.

### Filesystem

A pair of path sets control what the binary can see:

```toml
[sandbox]
read_only = ["root://", "root://vendor"]
writable  = ["root://build", "cache://temp"]
hidden    = ["/home/user/.ssh", "/home/user/.gnupg"]
```

| Backend    | Implementation                                                |
|------------|---------------------------------------------------------------|
| bwrap      | `--ro-bind` for read_only, `--bind` for writable, `--dir` for tmpfs mounts, omit hidden paths |
| landlock   | `LANDLOCK_ACCESS_FS_READ_DIR` + `WRITE_FILE` + `REMOVE_DIR` rules on allowed paths, deny by default  |
| seccomp    | Cannot enforce path-level restrictions (filesystem is not syscall-granular enough) |

Default filesystem policy for `--sandbox` (no explicit `[sandbox]`):

```
read_only = ["root://"]
writable  = ["cache://"]
hidden    = []
```

This means the binary can read the project tree but only write to cache.
Many binaries need a writable temp — that maps to `cache://temp` or `root://build`.

### IPC / PID / UTS

These are implicit in bwrap's `--unshare-all` and are always isolated when
bwrap is used. landlock/seccomp do not address these.

## Config: Stage-level `[sandbox]`

```toml
[[stages]]
name = "build-site"
type = "binary"
binary = "hugo"
params = { contentDir = "../content", destination = "build" }

[sandbox]
backend = "bwrap"          # optional: prefer specific backend
network = "loop"           # allow localhost only
caps = "none"
read_only = ["root://"]
writable  = ["root://build"]
hidden    = ["/home/user/.ssh"]
```

When `--sandbox` is passed on the CLI without a stage-level `[sandbox]`,
sensible defaults apply per binary type (see [Binary profiles](#binary-profiles)).

## Binary Profiles (`binaries/*.toml`)

Pre-authored profiles ship with predep, mapping known binaries to their
expected sandbox needs, arg schemas, and command-level refinements.

```toml
[binary.hugo]
description = "Static site generator"
homepage = "https://gohugo.io"

  [binary.hugo.sandbox]
  network = "loop"         # local dev server, no external deps
  read_only = ["root://"]
  writable  = ["cache://temp"]

  # Per-command refinement
  [binary.hugo.commands.serve]
  network = "full"         # must accept external connections
  description = "Hugo development server"

  [binary.hugo.commands.install]
  network = "full"         # downloads themes
  writable  = ["root://themes"]
  description = "Install Hugo theme"

  # Arg schema (from Layer 4 roadmap)
  [binary.hugo.args.contentDir]
  type = "path"
  safety = "safe"

  [binary.hugo.args.destination]
  type = "path"
  safety = "safe"

  [binary.hugo.args.cleanDestinationDir]
  type = "flag"
  safety = "warning"       # destructive but expected
```

```toml
[binary.bun]
description = "JavaScript runtime & package manager"
homepage = "https://bun.sh"

  [binary.bun.sandbox]
  network = "full"
  read_only = ["root://"]
  writable  = ["root://node_modules", "cache://bun"]

  # Commands that run arbitrary downloaded code
  [binary.bun.commands.install]
  safety = "dangerous"     # runs postinstall scripts
  sandbox = { network = "full", writable = ["root://"] }
  description = "Install npm dependencies (arbitrary code execution)"

  [binary.bun.commands.run]
  safety = "dangerous"     # runs user scripts
  sandbox = { network = "full", writable = ["root://build"] }
  description = "Run a JavaScript file"

  [binary.bun.args.cache]
  type = "path"
  safety = "safe"
```

## Binary Categorization

Each known binary is tagged with a category. Categories define default
sandbox rules (network, filesystem, capabilities) that the binary profile
inherits unless overridden:

| Category | Examples | Default network | Default writable | Safety |
|----------|----------|-----------------|------------------|--------|
| `lsp` | clangd, rust-analyzer, gopls, pyright | none | cache | Safe — read-only analysis |
| `formatter` | prettier, ruff, clang-format | none | cache → project | Safe — modifies source in place |
| `linter` | shellcheck, eslint, mypy, hadolint | none | cache | Safe — read-only analysis |
| `build-tool` | cmake, make, ninja, meson | none | build tree | Normal — writes build artifacts |
| `scm` | git | loop (clone) / none (status/diff) | project/.git | Normal — controlled network |
| `package-manager` | npm, bun, pip, cargo, go install | full | project root | DANGEROUS — runs remote code |
| `data-transfer` | curl, wget, scp, rsync | full | cache | Normal — I/O defined by args |
| `shell` | bash, sh, zsh, fish | full | full | DANGEROUS — equivalent to `run` |
| `utility` | ls, cat, rg, fd, find, grep, sed, awk | none | cache | Safe — read-only by default |
| `file-mutation` | rm, cp, mv, chmod, install | none | project | Normal — destructive by intent |
| `js-runtime` | node, deno, bun run | full | project build | DANGEROUS — arbitrary execution |
| `disk-util` | df, du, lsblk, blkid | none | none (read-only) | Suspicious — rarely needed in builds |
| `debug` | perf, strace, ltrace, gdb | none | cache | Dangerous — system introspection |

Categories serve as profile inheritance base:

```toml
# User override: promote a utility to full network
[binary.wget]
category = "utility"       # inherits utility defaults
network = "full"           # override: wget needs network
```

Category assignment is implicit (from built-in profiles) and overridable in
user profiles.

### Command matching

The profile system matches the argv of a `binary` stage to a command:

- If `params` contains a key matching a known command (e.g. `params.install = true`)
  or the first positional `args` token matches, the command-specific overrides
  merge on top of the base binary sandbox.
- Unknown commands fall back to the base binary sandbox.

### Built-in vs user profiles

| Source             | Path                              | Priority |
|--------------------|-----------------------------------|----------|
| Built-in profiles  | `<installdir>/binaries/*.toml`   | Lowest   |
| User profiles      | `root://binaries/*.toml`         | Medium   |
| Stage `[sandbox]`  | Inline in `predep.toml`          | Highest  |

User profiles extend or override built-in ones. Stage-level `[sandbox]` wins
over everything.

## CLI Usage

```bash
# Default sandbox (auto-detect bwrap/landlock, apply profile defaults)
predep --sandbox build-site

# Specific backend
predep --sandbox=bwrap build-site
predep --sandbox=landlock build-site

# Explicit stage sandbox config overrides
predep --sandbox --unrestricted-network build-site
```

### `--sandbox` without a stage `[sandbox]`

1. Look up the binary name in built-in + user profiles
2. Apply the matched profile's `[sandbox]` defaults
3. The binary executes within those constraints

If no profile exists, apply a universal-safe default:

```
read_only = ["root://"]
writable  = ["cache://"]
network = "none"
caps = "none"
```

This is maximally restrictive — the binary can read the project, write to
cache, but cannot reach the network or write anywhere else. If it fails,
the user adds a `[sandbox]` or profile to widen access.

## Backend Internals

### bwrap backend

Constructs the bwrap argv in a buffer, then passes it via a pipe to
bwrap's `--args FD` flag. This keeps the sandbox invocation invisible
in `ps aux` — the kernel sees only `bwrap --args 3` on the command line,
not the full mount list.

```cpp
// In bwrap_builder:
std::vector<std::string> args = {
    "/usr/bin/bwrap",
    "--unshare-all",
    "--args", "3",   // read remaining args from FD 3
    "--",
    binary,
};

// Append argv...

// When launching:
int pipefd[2];
pipe2(pipefd, O_CLOEXEC);
write(pipefd[1], args_str.data(), args_str.size());
close(pipefd[1]);

// Use posix_spawn_file_actions or fork+dup2 to bind pipefd[0] to FD 3
// in the child, then execvp bwrap.
```

Generated args structure:

```
--unshare-all
(--share-net | --unshare-net)
--die-with-parent
--proc /proc
--dev /dev
--tmpfs /tmp --tmpfs /var/tmp
--ro-bind <src> <dst>   (for each read_only path)
--bind <src> <dst>      (for each writable path)
--cap-drop ALL
--uid <uid> --gid <gid>
--chdir <cwd>
--setenv HOME <sandbox-home>
--setenv PATH <sandbox-path>
```

System paths are mounted read-only as well — terminfo, zoneinfo, SSL certs,
resolver config, fonts, and the specific binary itself (resolved via
`which()` + `realpath()`). Directories are recursively enumerated for
fine-grained `--ro-bind` rather than blanket `--ro-bind <dir> <dir>` which
would expose everything under that directory.

The `bwrap` binary is resolved at runtime via `which bwrap` / `platform::which()`.
If not found, fall through to next backend.

### Landlock backend

No subprocess wrapping needed. Before `execvp()` in `binary_action::resolve()`:

```cpp
int landlock_create_ruleset(...);
// For each allowed read path:
struct landlock_path_beneath_attr attr = {
    .allowed_access = LANDLOCK_ACCESS_FS_READ_DIR
                      | LANDLOCK_ACCESS_FS_READ_FILE,
    .parent_fd = open(path, O_PATH),
};
ioctl(ruleset_fd, LANDLOCK_ADD_RULE, &attr);
// For each allowed write path, add WRITE_FILE / REMOVE_DIR / etc.
// Then commit:
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
landlock_restrict_self(ruleset_fd, 0);
// Then execvp normally — kernel enforces the rules.
```

Since landlock only restricts **filesystem** access, network and capabilities
must be handled separately (or not at all — landlock-only sandbox means the
binary may still reach the network).

### Seccomp backend

Applied alongside bwrap or landlock (never standalone). Uses libseccomp:

- Default action: `SCMP_ACT_KILL` / `SCMP_ACT_ERRNO`
- Allowlisted: basic I/O, memory, scheduling, and whatever the binary needs
- Blocked: `mount`, `umount`, `swapon`, `reboot`, `init_module`,
  `delete_module`, `kexec_load`, `bpf`, `perf_event_open`, `ptrace`,
  `personality`, etc.

Network restriction is enforced by blocking `socket`, `connect`, `bind`,
`socketpair` (with exceptions for `AF_UNIX` when `network = "loop"`).

## Safety Analysis

### What this adds

| Threat                        | Before sandbox                  | After sandbox                                  |
|-------------------------------|-------------------------------- |------------------------------------------------|
| Binary reads `/etc/shadow`    | Full access                     | Blocked by landlock / bwrap `--ro-bind`        |
| Binary writes to `~/.ssh`     | Full access                     | Blocked by landlock / bwrap mount policy       |
| Binary phones home            | Full network                    | Blocked by bwrap `--unshare-net` / seccomp     |
| Binary loads kernel module    | Full capability set             | Blocked by bwrap `--cap-drop ALL` / seccomp    |
| Binary spawns child in PID ns | Full PID namespace              | Blocked by bwrap `--unshare-pid`               |
| Malicious postinstall script  | Full project + network access   | Restricted to declared paths + network policy  |

### What it does NOT add

- **Pure user-space sandboxing is not containment.** bwrap/landlock/seccomp
  are not VMs. A determined attacker with a kernel 0-day can still escape.
- **Landlock alone does not isolate network or capabilities.**
- **Seccomp is complex to author** — the allowlist must match the actual
  syscall footprint of the binary, which changes across versions.
- **TOCTOU on file operations** — landlock rules are checked at open() time;
  if the path changes between open and use, the rule may no longer apply.
- **Binary profiles are trust-by-name** — a profile for `hugo` applies to
  anything named `hugo`. Verification of the binary itself (SHA256, signature)
  is a separate concern.

## Build System Integration

Sandbox backends are conditionally compiled via `premake5.lua` options.
This keeps the binary lean for cloud/CI environments that don't need
sandboxing and may not have the required kernel APIs.

```lua
newoption {
    trigger = "sandbox",
    description = "Enable sandbox backends"
}

if _OPTIONS["sandbox"] then
    defines({ "WITH_SANDBOX" })
    filter("system:linux")
    files({ "src/sys/bwrap.cpp", "src/sys/landlock.cpp", "src/sys/seccomp.cpp" })
    links({ "seccomp" })
end
```

Per-backend granularity:

```
premake5 --sandbox                    # all backends on current platform
premake5 --sandbox-backends=bwrap     # specific backends only
premake5 --sandbox-backends=bwrap,landlock
```

Without the sandbox option, sandbox code is excluded entirely — the
`--sandbox` CLI flag is absent, `binary_action::resolve()` never wraps
execution, and the profile loader is not compiled.

At runtime, if predep was built without a backend and `--sandbox` is passed,
it errors with a clear message:

```
Error: predep was built without sandbox support.
Recompile with `premake5 --sandbox` to enable.
```

Built-in binary profiles (`.toml` files) are data-only, loaded at runtime.
They are always installed regardless of build options — the profile loader
simply returns "no profiles loaded" when built without `WITH_SANDBOX`.

## Implementation Order

1. **Landlock backend** (simpler, no subprocess wrapping)
   - `src/sys/landlock.h/cpp` — RAII ruleset builder
   - Plumb into `binary_action::resolve()`
   - Respect `[sandbox]` read_only / writable / hidden

2. **bwrap backend**
   - `src/sys/bwrap.h/cpp` — argv builder for bwrap subprocess
   - `--args FD` pipe approach (not temp files)
   - Availability detection via `which bwrap`
   - Binary resolution: `which()` + `realpath()` for fine-grained `--ro-bind`
   - System path enumeration (terminfo, zoneinfo, SSL certs, etc.)
   - Network / caps / IPC isolation

3. **Seccomp backend**
   - `src/sys/seccomp.h/cpp` — default syscall filter
   - Optional network restriction filter

4. **`--sandbox` CLI flag**
   - `src/cli/args.h/cpp` — add `--sandbox` (optional string: backend name)
   - Pass through to `runtime` / `binary_data`
   - On non-Linux platforms: warn and skip (sandbox unavailable)

5. **Binary profile system**
   - `src/cfg/binary_profile.h/cpp` — TOML loader for `binaries/*.toml`
   - Profile merging (built-in → user → stage)
   - Command-level refinement matching

6. **Built-in profiles** — ship `binaries/hugo.toml`, `binaries/bun.toml`,
   `binaries/premake5.lua`, etc.

7. **Update security model** — promote `binary` from `HIGH` to `NORMAL` when
   sandboxed; `DANGEROUS` when `--no-sandbox` or no profile match.
