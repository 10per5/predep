# Skill: Vendoring from GitHub branches / commit hashes

## What it is

For dependencies you need as **source** (to build in-tree, patch, or pin to an
exact commit), use a `[[vendor]]` entry in **git mode**: a `repo` URL plus a
`ref`. predep clones the repo and checks out the exact ref, recording it in a
`.predepgit` marker for idempotency. This is the reproducible alternative to
archive downloads when you want a specific branch tip, tag, or commit SHA.

## predep.toml shape

```toml
[[stages]]
name = "vendor"
type = "vendor"

# Pin to an exact commit SHA (most reproducible)
[[vendor]]
name = "corrade"
repo = "https://github.com/mosra/corrade.git"
ref  = "c028fd6e261a0665f72fbc75c7c034baf47747c6"
dest = "root://vendor/corrade"
builder = "corrade-cmake"

# Pin to a tag (stable release)
[[vendor]]
name = "magnum"
repo = "https://github.com/mosra/magnum.git"
ref  = "v2020.06"
dest = "root://vendor/magnum"
builder = "magnum-cmake"

# Track a branch tip (least reproducible — use sparingly)
[[vendor]]
name = "feature-branch-lib"
repo = "https://github.com/owner/lib.git"
ref  = "main"
dest = "root://vendor/lib"
depth = 1            # shallow clone to save bandwidth
```

## Fields

| Field | Default | Description |
| ----- | ------- | ----------- |
| `repo` | — | Git URL. Presence of this field puts the entry in **git mode** (URL/sha256 are ignored). |
| `ref` | — | Pinned `ref`: a 40-hex commit SHA, a tag (`v1.2.3`), or a branch name (`main`). |
| `depth` | `0` (full) | Shallow-clone depth. `1` = branch tip only. |
| `submodules` | `false` | Pass `--recursive` to clone submodules. |
| `dest` | `root://vendor/` | Destination directory (clone target). |
| `builder` | — | Build stage that compiles this source (see `vendor-libraries.md`). |

## How it resolves

1. `git clone [--depth N] [--recursive] <repo> <dest>` (with `--progress`, which
   predep renders as a progress bar).
2. `git -C <dest> checkout <ref>`.
3. If checkout fails (a 40-hex SHA may be absent from a shallow clone), predep
   does `git fetch origin <ref>` then checks out again.
4. Writes `<dest>/.predepgit` containing the resolved `ref`.

On re-run, predep compares the stored `.predepgit` ref to the configured `ref`:
- **Match** → skip (already cloned & pinned).
- **Mismatch** → removes the directory and re-clones.

## Idempotency & rebuilds

- The pinned ref is part of the build signature of any `cmake`/`premake5` stage
  that builds it. Changing `ref` invalidates the build and forces a rebuild
  (see `building-cmake-premake.md`).
- A branch ref (`main`) is *not* memoized as "changed" automatically — predep
  only compares the stored string. Use a tag or SHA for reproducible rebuilds.

## Gotchas

- **git mode vs archive mode:** setting `repo` switches the entry to git mode;
  `url`/`sha256` on the same entry are ignored. Use one or the other per entry.
- **`dest` is the clone root**, not a parent. `dest = "root://vendor/corrade"`
  clones directly into that directory (no extra inner folder).
- **Shallow + SHA:** a `depth > 0` clone may not contain an arbitrary historical
  SHA — predep's `fetch origin <ref>` fallback handles this, but a full clone
  (`depth = 0`) is safest for arbitrary SHAs.
- **Private repos:** git clone uses the ambient credentials (SSH agent / token in
  URL). predep does not manage auth.
