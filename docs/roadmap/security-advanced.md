---
title: Security — Advanced (Planned)
weight: 60
---

# Security — Advanced (Planned)

Features in this document are **not yet implemented**. Items are marked as
**planned** (scheduled), **tentative** (designed but unscheduled), or
**speculative** (exploratory, may not ship).

## Layer 4: Binary profiles — planned

Binary metadata files (`binaries/*.toml`) combine arg schema, safety tagging,
and per-command sandbox defaults into a single profile per known binary.
See `sandbox.md` for the full design.

```toml
[binary.hugo]
description = "Static site generator"

  [binary.hugo.sandbox]
  network = "loop"
  read_only = ["root://"]
  writable  = ["cache://temp"]

  [binary.hugo.args.contentDir]
  type = "path"
  safety = "safe"

  [binary.hugo.args.destination]
  type = "path"
  safety = "safe"

  [binary.hugo.args.cleanDestinationDir]
  type = "flag"
  safety = "dangerous"
```

Until profiles are implemented, all `params` and `args` on `binary` stages
are treated as DANGEROUS:

| Field    | Structure             | Risk                                      |
| -------- | --------------------- | ----------------------------------------- |
| `params` | `--key value` pairs   | DANGEROUS — no schema, no type validation |
| `args`   | Free-form argv tokens | DANGEROUS — no structure, no validation   |

## Layer 5: Shape validation — planned

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

## Per-stage trust — speculative

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

Not currently planned for the near term. The per-file model already catches
the core attack surface (any modified file re-prompts); per-stage would
refine precision.

## Harmful SHA256 database — speculative

A future version of predep could maintain a database of known harmful SHA256
hashes — combinations of config hash, stage commands, and binary invocations
that are known to be malicious. This database could be distributed as a
signature file similar to antivirus scanners, allowing predep to reject
known-dangerous configurations before prompting the user.

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

No timeline. The config trust prompt + SHA256 caching model is sufficient
for the current threat model.

## Adaptive security model — planned

Replace the current Layer 0 (SHA256-based config trust prompt + blanket
`run`/`binary` block) with a context-aware model that avoids hash tracking
entirely.

### Current problems

- SHA256 whack-a-mole: every config change invalidates trust, requiring
  re-prompting. The hash is checked eagerly — it does not compose well with
  included manifests, platform overrides, or generated configs.
- `--privileged` must be tracked everywhere, threading a boolean through
  the entire runtime. It's one global switch — no granularity.
- Eager denial: the security prompt fires before any execution. There's no
  "let it run, but contain it" middle ground.
- The risk table (`DANGEROUS`/`HIGH`/`NORMAL`/`SAFE`) is documentation
  only — no code enforces different treatment per level.

### Proposed model

Two axes determine how a stage executes:

```
                    Interactive (DE/TTY)          Headless (CI/automation)
                    ─────────────────────         ─────────────────────────
  run / binary      sandboxed (auto)              run normally (auto)
                    --privileged → unrestricted   --privileged → unrestricted

  everything else   run normally                  run normally
```

No SHA256 trust store, no pre-flight "will this be dangerous?" prompt.
Instead:

1. **Binary signatures** — known-good binaries are identified by SHA256 of
   the binary itself (not the config). The first time `hugo` runs, predep
   records its hash. On subsequent runs, if the hash matches, the profile
   is trusted — no prompt. If the hash changes, the old profile is
   invalidated and a warning is shown.

2. **Per-call danger assessment** — each `run` command or `binary` invocation
   is evaluated at execution time, not in a pre-scan. The action checks
   whether it's in a sandbox-capable environment. If yes, it wraps the
   execution in the sandbox backend (bwrap/landlock/seccomp). If not, it
   runs unconfined (headless/CI mode).

3. **`--privileged` becomes opt-in escalation** — instead of a runtime
   boolean threaded everywhere, `--privileged` means "skip sandboxing for
   this run." No hash checking, no trust prefs. In headless mode without
   `--privileged`, dangerous stages still run — they just run with the
   default confinement of the platform.

4. **Binary profiles serve as trust anchors** — the profile declares what a
   binary should do. When the binary's hash matches the profile's recorded
   hash, the profile's safety assertions (arg schemas, sandbox rules) are
   trusted. No profile = run with universal-safe defaults and warn.

### Benefits

- No SHA256 tracking of config files — eliminates the entire trust prefs
  system, expiry, include trust complexity
- `--privileged` is purely a sandbox bypass flag — no second meaning, no
  threading through every layer
- Headless/CI works without `--privileged` — dangerous stages run without
  sandbox (which is fine — CI is ephemeral and already isolated)
- Interactive gets real containment — sandbox failure is recoverable, unlike
  a prompt rejection which stops the pipeline
- Binary identity is the trust primitive, not config content hash

### What this replaces

- Layer 0 config trust prompt (`security::check_run_stages()`)
- SHA256 caching + trust expiry + include trust
- `--privileged` as a hash bypass mechanism
- The "DANGEROUS" risk label as an early-blocking gate
- The harmful SHA256 database concept (superseded)

### Transition path

1. Phase 1: implement sandbox backends (bwrap/landlock/seccomp)
2. Phase 2: `run`/`binary` actions attempt sandbox in interactive mode;
   fall back to unconfined in headless (no prompts)
3. Phase 3: binary profiles with hash anchoring; deprecate SHA256 trust prefs
4. Phase 4: remove the config trust prompt and trust prefs system entirely

## Distro package manager integration — tentative

The current `sudo cp` approach for install/uninstall is portable but not
idiomatic for any platform. Future versions could support generating native
packages so installed artifacts integrate with the system package manager:

| Platform / Distro | Package format | Toolchain |
|---|---|---|
| Debian / Ubuntu | `.deb` | `dpkg-deb`, `equivs` |
| Fedora / RHEL | `.rpm` | `rpmbuild`, `fpm` |
| Arch Linux | PKGBUILD / `.pkg.tar.zst` | `makepkg` |
| Alpine | `.apk` | `abuild` |
| macOS | Homebrew formula | `brew` tap |
| macOS | `.pkg` | `pkgbuild` |
| Windows | `.msi` / NuGet | `wixtoolset` |

This would replace `sudo cp` with `dpkg -i`, `rpm -i`, `pacman -U`, etc.
Selection would auto-detect the platform (`.deb` on Debian, `.rpm` on Fedora,
Homebrew on macOS), overridable via `--pkg deb`. The plain `sudo cp` mode
stays as the portable fallback when no native backend is available or
requested.
