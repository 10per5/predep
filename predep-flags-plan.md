# predep `flags` for the premake5 stage — Design & Plan

## Summary

Add an optional `flags` array to the `premake5` build stage. Each entry is
appended to the generated `premake5 <action>` command so callers can pass extra
generator options (e.g. `["--tls"]`). As part of this, harden the whole
premake5 command (generate → make/msbuild → strip) against shell command
injection, since every one of those steps is executed through
`process::shell()` (`/bin/sh -c`).

## Status

| # | Change | State |
|---|--------|-------|
| 1 | `stage.h`: add `flags` to `premake5_entry` | ✅ done |
| 2 | `premake5_action.cpp` `parse()`: defaults + platform flags | ✅ done |
| 3 | `premake5_action.cpp` `resolve()`: append flags to `premake5` cmd | ✅ done |
| 4 | `config_loader.cpp` root-level platform override: read flags | ✅ done |
| 5 | `docs/stages/building.md`: `flags` row + security note | ✅ done |
| 6 | `skills/building-stages.md`: `flags` subsection + security note | ✅ done |
| 7 | `premake5_action.cpp`: `sanitize_flags` + `security::is_shell_safe(flag, /*dash*/true)` guard — drop unsafe flags, warn | ✅ done |
| 8 | `premake5_action.cpp`: scalar hardening for `action`/`config`/`project` via `security::is_shell_safe` | ✅ done |

## Changes

- `src/data/stage.h` — `premake5_entry` gains `std::vector<std::string> flags;`
  (carried on both the defaults and the `platform_entry<premake5_entry>` override).
- `src/security/security.h` / `security.cpp` — adds the single shared util
  `bool is_shell_safe(const std::string &token, bool require_leading_dash = false)`
  used by **both** the flag path and the scalar path (no duplicated validation
  loop). Safe tokens contain only `[A-Za-z0-9._:/+=-]`; the optional
  `require_leading_dash` enforces a `--flag` shape.
- `src/action/premake5_action.h` — declares
  `static std::vector<std::string> sanitize_flags(const std::vector<std::string>&)`.
- `src/action/premake5_action.cpp`
  - `parse()` reads `flags` from `defaults` and from each `platform.*` override
    (platform override **replaces** the default list, consistent with scalars).
  - `sanitize_flags()` keeps only tokens passing
    `security::is_shell_safe(f, /*require_leading_dash=*/true)`.
  - `resolve()` appends guarded flags and rejects an unsafe `action` (always),
    `config` (when set), and `project` (Windows msbuild branch) via
    `security::is_shell_safe(...)` with a security error before evaluation.
- `src/cfg/config_loader.cpp` — reads `flags` under root-level `platform.*`
  overrides for the premake5 stage and merges them into the platform entry.
- `docs/stages/building.md` — `flags` field row + Security note covering `flags`
  **and** the `action`/`config`/`project` scalars.
- `skills/building-stages.md` — `premake5` `flags` subsection + Security note.
- `tests/ut/test_actions.cpp` + `tests/fixtures/premake5/predep.toml` — unit tests
  for parse (defaults + platform), `sanitize_flags`, and `security::is_shell_safe`.

## Decisions

- **Premake only.** `flags` are appended to the `premake5 <action>` generation
  step, not the `make` step (consistent with the original plan to keep the make
  invocation driven by `config` only).
- **Platform override replaces.** `[platform.*].flags` replaces the default
  `flags` list, matching how scalar fields behave on platform override.
- **Raw tokens / security (GUARDED).** Each `flags` entry is concatenated into
  the `premake5 <action>` command that runs via `sh -c`. A flag containing
  spaces, quotes, or shell metacharacters (`; & | $ \` > < ( ) { } [ ] * ? ! \`)
  is a **command-injection vector**. `resolve()` validates every flag with
  `security::is_shell_safe(f, /*require_leading_dash=*/true)` and keeps only
  `--word-args-like-this` tokens; unsafe flags are **discarded and never
  evaluated**, and the user is warned that spaces are not currently allowed for
  security. `sanitize_flags()` exposes the same check for tests.
- **Whole-command hardening (GUARDED).** The `action`, `config`, and `project`
  scalar fields are also concatenated into the `sh -c` command (premake action,
  and the `make config=` / `msbuild /p:Configuration=` build step). Unlike
  `flags`, they can't be safely dropped, so an unsafe value is **rejected and the
  stage fails** with a security error. The single shared validator
  `security::is_shell_safe(token)` (shell-safe word `[A-Za-z0-9._:/+=-]`) is used
  for both the flag path and the scalar path, called in `resolve()` for `action`
  (always), `config` (when set), and `project` (Windows msbuild branch). This
  closes the remaining manifest-value injection paths in the generate → build →
  strip flow, with the validation rule defined in exactly one place.
  - **Out of scope:** the resolved **strip target path** (`target` / `outputs`)
    is still passed unquoted into `strip <path>`. That is a manifest-controlled
    filesystem path; hardening it correctly requires shell-quoting (not
    rejection, to avoid breaking Windows `\` paths), so it is left as a known
    limitation rather than silently dropping valid paths.
- **Tests pending harness.** The unit tests are written against `doctest`, but
  the harness (`tests/main.cpp`, `tests/premake5.lua`, vendored
  `tests/vendor/doctest/doctest.h`) is not scaffolded in the tree, so the suite
  does not yet build/run. This is a separate scaffolding task.

## Follow-up

- Scaffold the doctest harness so the premake5 unit tests actually build/run.
- Optionally shell-quote the strip target path instead of relying on
  manifest-authors avoiding metacharacters.
- If desired, extend the same guard to other shell-executing stages
  (`run` is shell-by-design; `docker` builds from `recipe`/`target`/`dest`).
