---
title: Security Model
weight: 50
---

# Security Model

predep's security model distinguishes three execution layers, each with
different guarantees and risk levels.

## Stage Types by Risk

| Stage type                  | Execution engine            | Shell?         | Risk level                          |
| --------------------------- | --------------------------- | -------------- | ----------------------------------- |
| `run`                       | `/bin/sh -c` / `cmd.exe /c` | Yes            | DANGEROUS                           |
| `binary`                    | `execvp` / `CreateProcessA` | No             | HIGH — schema not yet implemented   |
| `disabled`                  | None (no-op)                | —              | SAFE                                |
| `vendor`/`fetch`/`resource` | None (download only)        | —              | SAFE                                |
| `docker`                    | Docker CLI                  | No (arg-based) | NORMAL                              |
| `group`                     | None (no-op)                | —              | SAFE                                |
| `package`                   | None (file ops)             | —              | SAFE                                |
| `copy`                     | None (file ops)             | —              | SAFE — root:// only, no cache://   |
| `install`                   | None (file ops) / `sudo cp` | No             | NORMAL — file ops, sudo escalation  |
| `uninstall`                 | None (file ops) / `sudo rm` | No             | NORMAL — file ops, sudo escalation  |
| `premake5`                  | `execvp` / `CreateProcessA` | No             | NORMAL — known binary, schema-bound |

> **Note on package managers:** Commands like `npm install`, `pip install`,
> `bun install`, `gem install`, `cargo install`, `go install`, and similar
> package manager invocations execute arbitrary code from remote registries
> during their install lifecycle (pre/post-install scripts, build hooks).
> Even when run via a `binary` stage (which avoids shell injection), the
> package manager itself is a trusted binary but _the packages it installs
> are not_. Treat any stage that invokes a package manager as equivalent to
> `run` in terms of trust — the install scripts have full access to your
> project tree, environment, network, and credentials. If you wouldn't
> `curl | sh` the package, don't `npm install` it either.

## Security Layers

### Layer 0: Config trust prompt

Implemented in `security::check_run_stages()`. Before any stage executes,
predep scans all stages and warns the user listing every `run` and `binary`
command that will be executed. The user must explicitly confirm. In
non-interactive mode (no TTY), execution is blocked unless `--privileged`
is passed.

Once confirmed, the config file's SHA256 is cached as trusted so repeated
runs don't re-prompt (unless the file changes). The file path is recorded
alongside the hash so the user can see which configuration was approved.

#### Prompt matrix

Whether a security prompt blocks execution depends on three factors: the
safety level of the operation, whether `--privileged` is active (selects the
prompter type), and whether the configuration SHA256 is already trusted.
Trusted configs skip the prompt entirely (auto-ok across all levels).

| Level | Interactive (TTY) | Interactive (headless) | Privileged (TTY) | Privileged (headless) |
|-------|-------------------|------------------------|------------------|-----------------------|
| `safe` | auto-ok | auto-ok | auto-ok | auto-ok |
| `warning` | draw_box → auto-ok | auto-ok | draw_box → auto-ok | auto-ok |
| `dangerous` | draw_box + y/N | error: use `--privileged` | draw_box + countdown → proceed | countdown → proceed |
| `critical` | draw_box + error: need `--privileged` | error: use `--privileged` | draw_box + y/N → countdown → proceed | countdown → proceed |

- `warning` is reserved for future use — current callers always use `dangerous`.
- `countdown` is a 2-second stderr countdown before execution proceeds.

#### Trust expiry

Trusted SHA256 entries expire after a configurable period (default: 7 days)
unless marked `permanent`. Expired entries are automatically cleared on the
next `resolve()` call:

```toml
trust_time_minutes = 10080   # 7 days

[[trusted]]
sha256 = "abc123..."
trusted_at = 1718000000
permanent = false
paths = ["/home/user/project/predep.toml"]

[[trusted]]
sha256 = "def456..."
trusted_at = 1718086400
permanent = true             # never expires
paths = [
    "/home/user/project/trusted/predep.toml",
    "/home/user/project/vendored/predep.toml",
]
```

The prefs file is located at `cache://preferences/trusted.toml`. Users can
edit it directly:

```bash
cat "$(predep --cache-dir)/preferences/trusted.toml"
```

#### Include trust

When a root manifest uses `[[include]]`, each included file is tracked and
hashed independently. Trust is **per-file**, not aggregate:

- A previously trusted included file can change without invalidating the root
  config's trust — it simply becomes untrusted on the next run.
- If an included file contains new `run` or `binary` stages, the prompt lists
  its path and asks you to review before confirming.
- Trusting the prompt adds only the **untrusted files'** SHA256 hashes to the
  trust store. Previously trusted files remain trusted without re-prompting.
- If the included file has no executable stages, its SHA256 is still tracked
  but no prompt is shown — nothing to execute.

### Layer 1: No interpolation on binary args

`${VAR}` substitution is strictly limited to **URLs and paths** in download
entries (`fetch_entry::url`, `fetch_entry::dest`). Binary stage `params`,
`args`, and the `binary` name itself are **never interpolated**. This
prevents smuggling shell metacharacters through variable expansion into an
argument vector.

### Layer 2: No shell for binary stages

`process::run()` on POSIX uses `execvp()` — no `/bin/sh -c`, no shell
operators (`$()`, `` ` ``, `;`, `|`, `&`). On Windows it uses
`CreateProcessA` with a flat command line. Both treat every argument as
a literal string — no metacharacters are interpreted.

This means `--flag=--flag; rm -rf /` in a `binary` stage reaches the
child process as a literal argv token. The shell never sees it.

### Layer 3: PATH confinement

Binary stages check `cache://bin` (with cross-platform `.exe` suffix via
`platform::exe_name()`) before falling back to system PATH. This lets
projects vendor their own tooling without depending on globally installed
binaries — and prevents accidentally running a different binary with the
same name from elsewhere on the system.

### Layer 6: Path confinement

All `dest` fields (`fetch_entry::dest`, `docker_entry::dest`, `copy_data`
sources/dests) and artifact source paths are validated against the project
root and cache directory. Any resolved path that falls outside `ctx.root` or
`ctx.cache_dir` is **rejected** unless `--privileged` is used. The `copy` stage
is stricter: it permits the project root only and rejects `cache://` and any
escape path as a hard config error that `--privileged` cannot bypass.

Paths are expressed using the `root://` and `cache://` prefixes, which
resolve to the project root and platform cache directory respectively.
Absolute paths like `/var/lib/evil` or `/root/.ssh/authorized_keys` are
blocked at the security check layer.

#### `--privileged` flag

To allow system-path access, pass `--privileged`:

```bash
predep --privileged <stage>
```

When `--privileged` is used:

1. The config file's SHA256 **must already be in the trusted prefs** — either
   added by a previous non-privileged run with a security prompt confirmation,
   or manually inserted into `cache://preferences/trusted.toml`.
2. If the SHA256 is not trusted, predep fails with a message showing the
   prefs file path so the user can pre-approve it.
3. In interactive (TTY) mode, a prominent warning is shown with a **2-second
   countdown** before execution begins.

This ensures that `--privileged` cannot be used on an untrusted config
without explicit prior approval.

`--privileged` replaces the earlier `--force` flag, which was never fully
wired. Unlike `--force`, `--privileged` does not skip security prompts for
`run`/`binary` stages — those still require interactive confirmation.

### Sudo escalation (`install` / `uninstall` stages)

When an `install` or `uninstall` stage writes to a system directory (e.g.
`/usr/local/bin`) and the current user lacks write permission, predep
automatically falls back to `sudo cp` / `sudo rm -f` / `sudo mkdir -p` for
the individual file operations. This is the standard `make install` pattern.

Each sudo invocation runs as a separate subprocess via `execvp("sudo", ...)`.
Once the subprocess completes, no elevated privileges remain — sudo's
credential cache may persist (controlled by `sudoers(5)` `timestamp_timeout`),
but the predep process itself never holds root privileges.

The `--privileged` flag is not required for sudo fallback — sudo prompts for
the user's password via the TTY independently. However, if the config also
references paths outside the project root (e.g. `dir = "/opt/myapp"`),
`--privileged` is still needed for the path confinement check.

#### Credential cache hardening

To prevent a malicious stage (e.g. a `run` stage) from inheriting sudo
credentials cached by install/uninstall, each resolve call:

1. Runs `sudo -K` **before** any file operations — clears any stale
   credentials inherited from outside the predep process.
2. Runs `sudo -K` **after** all operations — removes the credential cache
   created by our own sudo invocations, so subsequent stages cannot execute
   privileged commands without a fresh password prompt.

This ensures that sudo credentials exist only for the duration of the
install/uninstall resolve call itself, never across stages.

#### Root / sudo startup guard

Implemented in `security::check_root_sudo()`. When predep starts, the
process verifies it is not running with elevated privileges:

| Condition | Behavior |
|-----------|----------|
| `getuid() == 0` + `SUDO_USER` set | Running via `sudo`. Error: "Do not run predep with sudo." |
| `getuid() == 0` + `SUDO_USER` unset | Running as root directly. Error: "Do not run predep as root." |
| `getuid() != 0` | Normal user — allowed. |

The guard is bypassed when `--privileged` is combined with a non-TTY session
(headless/CI mode). This permits containerized or automated deployments that
deliberately run as root with `--privileged`.

This prevents the most dangerous attack vector: a config with `run` stages
executing arbitrary commands as root because the user accidentally ran
`sudo predep`.

| File operation | Direct call | Sudo fallback |
|----------------|-------------|---------------|
| Copy file | `fs::copy()` | `sudo cp <src> <dst>` |
| Copy directory | `fs::copy()` recursive | `sudo cp -r <src> <dst>` |
| Remove file | `fs::remove()` | `sudo rm -f <path>` |
| Create directory | `fs::create_directories()` | `sudo mkdir -p <path>` |
| Create symlink | `fs::create_symlink()` | `sudo ln -sf <target> <link>` |
| Remove symlink | `fs::remove()` | `sudo rm -f <link>` |

Sudo escalation is **not available on Windows** — install/uninstall on
Windows must target user-writable directories or run as Administrator.

## Not yet implemented

The following security layers are designed but not implemented:

- **Layer 4: Binary profiles** — arg schema, safety tagging, per-command
  sandbox defaults. See `roadmap/security-advanced.md`.
- **Layer 5: Shape validation** — argv validation gates (null bytes, token
  length, param key format, shell metacharacters, path separators in binary
  name). See `roadmap/security-advanced.md`.
- **Sandboxing** (`--sandbox`) — bwrap/landlock/seccomp backends. See
  `sandbox.md` for the architecture and implementation plan.

## Sandboxing

See `sandbox.md` for the full sandbox architecture (bwrap/landlock/seccomp),
binary profiles, and build system integration.
