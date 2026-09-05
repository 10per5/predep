# Changelog

## Entry format

Each entry follows the pattern below so updates stay consistent:

```
- YYYY-MM-DD — <branch/version>(<scope>): <short summary>
```

* `YYYY-MM-DD` — date of the change (ISO 8601)


* `<branch/version>` — the branch or version the change was made on (e.g. `v0.0.2p3`)


* `<scope>` — affected area (e.g. `vendor`, `security`, `parser`)


* `<short summary>` — what changed and why, in one line

## Entries

* 2026-09-05 — v0.0.2p3(vendor): add git source pull + pin and a generic `cmake` build stage. `[[vendor]]` entries gain `repo`/`ref`/`depth`/`submodules` to clone and pin at an exact SHA or tag, and a `builder` field that links the entry to a build stage. New `[[stages]] type = "cmake"` configures/builds/installs via `flagsOn`, `flagsOff`, `configurable`, `installPrefix`, and `installPrefixVars` (no raw args array). The `builder` link is auto-wired as a dependency on the vendor stage.