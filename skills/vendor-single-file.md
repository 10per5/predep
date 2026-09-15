# Skill: Vendoring a single file with a hash

## What it is

Some dependencies are a single header (or a single script) — no build step
needed, just drop the file into the tree. Use a `[[vendor]]` entry with a
direct file URL and a `sha256`. predep downloads it, verifies the hash, and is
done. No `builder`, no extraction.

This is ideal for header-only C++ libs (toml++, CLI11, doctest, single-file
libraries) and similar single-artifact deps.

## predep.toml shape

```toml
[[stages]]
name = "vendor"
type = "vendor"

[[vendor]]
name = "tomlpp"
url  = "https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp"
sha256 = "6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e"
dest = "root://vendor/"
create_directory = true
```

## How it resolves

- `url` points at the **raw file**, not an archive. predep detects the file is
  not `.tar.gz`/`.tgz`/`.zip` and skips extraction.
- `create_directory = true` nests the file under `root://vendor/tomlpp/` (so it
  becomes `vendor/tomlpp/toml.hpp`). Omit it to place the file directly at
  `root://vendor/toml.hpp`.
- `sha256` is verified after download. On mismatch predep re-downloads.
- If `sha256` is **omitted**, predep logs the computed hash on first download —
  copy it back into the config to lock the file down.

## GitHub raw URLs

Prefer a pinned tag/commit over a branch for reproducibility:

```
# Good — pinned to a tag (and a commit-resolved raw path is even better)
https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp

# Risky — a branch moves under you
https://raw.githubusercontent.com/marzer/tomlplusplus/master/toml.hpp
```

For a file deep in a repo at an exact commit, use the commit SHA in the path:

```
https://raw.githubusercontent.com/OWNER/REPO/<40-hex-SHA>/path/to/file.hpp
```

## Variable interpolation

Pin the version with `${VAR}` instead of hard-coding it twice:

```toml
[[vendor]]
name = "tomlpp"
url  = "https://raw.githubusercontent.com/marzer/tomlplusplus/v${VERSION}/toml.hpp"
sha256 = "6b51...783e"
dest = "root://vendor/"
create_directory = true
variables = { version = "3.4.0" }   # or: version = "3.4.0"
```

Matching is case-insensitive: `version = "3.4.0"` resolves `${VERSION}`.

## Gotchas

- **No `builder`** on file entries — there is nothing to compile. If you need to
  build the header into a library, that's a different skill (`vendor-libraries.md`).
- **No `extract`** — extraction only applies to archives. A lone `.hpp` is copied
  as-is.
- **`output_name`** renames the file if the URL basename isn't what you want on
  disk (e.g. `version.h` from a `?raw=true` URL).
