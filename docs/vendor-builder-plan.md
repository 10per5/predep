# Plan: Vendor builder via a generic `cmake` stage + `builder` link

## Goal

- `[[vendor]]` entries **only** pull source and pin it (git clone at an exact
  SHA/tag, or archive download + extract). No build logic in the vendor action.
- A new, generic `[[stages]] type = "cmake"` stage performs cmake
  configure → build → install. It is reusable for *any* cmake project
  (vendors **and** the main project later), so no build code is duplicated.
- A vendor entry declares `builder = "stageName"` to mark which stage must run
  to build it. This composes with `cmake` **or** `premake5` builders without
  extra code in the vendor step.

## TOML shape

```toml
[[stages]]
name = "vendor"
type = "vendor"

[[vendor]]
name = "corrade"
repo = "https://github.com/mosra/corrade.git"
ref  = "c028fd6e261a0665f72fbc75c7c034baf47747c6"   # exact SHA pin
dest = "root://vendor/corrade"
builder = "corrade-cmake"

[[vendor]]
name = "magnum"
repo = "https://github.com/mosra/magnum.git"
ref  = "f239823e9afd27ab0ecf28e1d567831e3c62a25e"
dest = "root://vendor/magnum"
builder = "magnum-cmake"

[[stages]]
name = "corrade-cmake"
type = "cmake"
source           = "root://vendor/corrade"
build_dir        = "root://vendor/corrade/build-corrade"
config           = "Release"
installPrefix    = "root://vendor/prefix"
flagsOn          = ["CMAKE_POSITION_INDEPENDENT_CODE"]
installPrefixVars = ["CORRADE_ROOT"]     # → -DCORRADE_ROOT=<installPrefix>
install          = true

[[stages]]
name = "magnum-cmake"
type = "cmake"
source           = "root://vendor/magnum"
build_dir        = "root://vendor/magnum/build-magnum"
installPrefix    = "root://vendor/prefix"
flagsOn          = ["MAGNUM_TARGET_VK", "MAGNUM_WITH_GLTFIMPORTER",
                    "MAGNUM_WITH_ASSIMPIMPORTER", "MAGNUM_WITH_STBIMAGEIMPORTER",
                    "MAGNUM_WITH_TRADE"]
installPrefixVars = ["CORRADE_ROOT"]
configurable     = ["SOME_KEY=some_value"]   # → -DSOME_KEY=some_value
flagsOff         = ["MAGNUM_WITH_DISABLED"]  # → -DMAGNUM_WITH_DISABLED=OFF
```

- `builder = "corrade-cmake"` on the vendor entry links it to the cmake stage.
  The engine auto-wires the dependency (the `cmake` stage runs *after* the
  vendor source is pulled), so an explicit `depends = ["vendor"]` is optional.
- `builder` can point to any build stage — e.g. `builder = "corrade-premake"`
  for a `premake5` stage — so vendors are buildable with cmake or premake
  interchangeably.
- Archive vendor entries (with `url`/`sha256`) simply omit `builder`.

## Data model (`src/data/stage.h`)

`fetch_entry` — add git pull/pin fields and the `builder` link; **remove** any
build sub-table (build now lives in the `cmake` stage):

```cpp
struct fetch_entry {
    // ... existing archive fields: fetch_type, name, url, dest, sha256,
    //     output_name, extract, create_directory, include, exclude, vars, platform ...

    // new: git source acquisition
    std::string repo;        // git URL (presence ⇒ git entry)
    std::string ref;         // pinned SHA or tag
    int         depth = 0;   // shallow clone depth (0 = full)
    bool        submodules = false;

    // new: link to the build stage that compiles this vendor
    std::string builder;     // stage name (e.g. "corrade-cmake")
};
```

New cmake stage (mirrors `premake5_data : buildable_data`):

```cpp
struct cmake_entry {
    std::string source;      // source dir (e.g. root://vendor/corrade)
    std::string build_dir;   // default: <source>/build
    std::string config = "Release";
    std::string installPrefix;                 // → CMAKE_INSTALL_PREFIX (default <source>/prefix)
    std::vector<std::string> flagsOn;          // → -D<flag>=ON
    std::vector<std::string> flagsOff;         // → -D<flag>=OFF
    std::vector<std::string> configurable;     // → -D<KEY>=<VALUE> (KEY=VALUE strings)
    std::vector<std::string> installPrefixVars; // → -D<var>=<installPrefix> (e.g. CORRADE_ROOT)
    std::vector<std::string> targets;          // empty = default target
    bool install = true;
};

struct cmake_data : buildable_data {
    cmake_entry defaults;
    std::map<platform_type, platform_entry<cmake_entry>> platform;
};
```

Add `stage_type::cmake` plus `stage_from_string`/`to_string` entries
(`src/data/stage.h` + `stage.cpp`).

## Parser

**`download_action::parse_entry`** (`src/action/download_action.cpp:79`) — read
git + builder fields:

```cpp
fe.repo       = elem.get_string("repo");
fe.ref        = elem.get_string("ref");
fe.depth      = (int)elem.get_int("depth", 0);
fe.submodules = elem.get_bool_flex("submodules", false);
fe.builder    = elem.get_string("builder");
```

**`cmake_action::parse`** (new) — read `cmake_entry` from the stage element,
including `[platform.*]` overrides (same pattern as `premake5_action::parse`,
`src/action/premake5_action.cpp:11`).

**`builder` auto-wire** (`src/cfg/config_loader.cpp`) — after stages + vendor
entries are parsed, for every vendor entry with a non-empty `builder`:
find the named stage; if missing, emit an error; otherwise ensure `"vendor"`
is in that stage's `depends` (append if absent). This keeps the manifest
declarative without repeating `depends`.

## Actions

**`download_action`** — git branch in `resolve_entries`/`check_entries`:
```
git clone [--depth N] [--recursive] <repo> <dest>
git -C <dest> checkout <ref>
```
- 40-hex `ref` ⇒ `git fetch origin <ref>` first (shallow clones can't reach a
  historical SHA); tags/branches may use `--depth`.
- Write `<dest>/.predepgit` marker holding the resolved `ref` (idempotency);
  on `ref` mismatch, re-fetch/checkout.
- Archive entries unchanged.

**`cmake_action`** (new) — generic build, no vendor knowledge. Flags are
assembled from the structured fields (no raw `args` array):
```
-DCMAKE_BUILD_TYPE=<config>
-DCMAKE_INSTALL_PREFIX=<installPrefix>
-D<flag>=ON            for each flagsOn
-D<flag>=OFF           for each flagsOff
-D<KEY>=<VALUE>        for each "KEY=VALUE" in configurable
-D<var>=<installPrefix> for each installPrefixVars (e.g. CORRADE_ROOT)
```
then:
```
cmake -S <source> -B <build_dir> <above -D flags...>
cmake --build <build_dir> -j
cmake --install <build_dir>      # if install
```
`${VAR}` interpolation (existing `config_loader::interpolate`) applies to
`source`, `build_dir`, `installPrefix`, `configurable` values, and
`installPrefixVars`.
- `resolve_cwd`/`confirm_build_context` reused from `action` base (like
  `premake5_action::resolve`, `src/action/premake5_action.cpp:82`).
- `is_resolved`: `<prefix>` outputs exist (and `<build_dir>` present) ⇒ skip.

## Files to change

| File | Change |
|------|--------|
| `src/data/stage.h` + `stage.cpp` | `fetch_entry`: `repo`,`ref`,`depth`,`submodules`,`builder`; new `cmake_entry`/`cmake_data`; `stage_type::cmake`; `stage_from_string`/`to_string` |
| `src/action/download_action.{h,cpp}` | parse git + `builder`; git clone/checkout + `.predepgit` marker; **drop** build logic |
| `src/action/cmake_action.{h,cpp}` | new generic cmake build stage |
| `src/cfg/config_loader.cpp` | `cmake` branch in `parse_stages`; `builder` auto-wire to `vendor` stage |
| `src/engine/resolver.cpp` | register `m_actions[stage_type::cmake]` |
| `src/sys/process.{h,cpp}` | optional thin `git()` / `cmake()` helpers (or reuse `process::run`) |
| `docs/` | document `cmake` stage + `builder` link |
| `tests/vendor`, `tests/it` | fixture: pin a repo, build via `cmake` stage, idempotent rebuild |

## Answers to earlier questions

- **Where is the repo/SHA specifier?** Added as inline `repo` + `ref` on a
  `[[vendor]]` entry (no such field existed; `sha256` only verifies archives).
- **Steps that build with cmake?** None today (only `premake5` + `run`). This
  adds a reusable `cmake` stage — the foundation for adapting cmake projects
  to predep, vendor or otherwise.

## Follow-ups (out of scope for v1)

- Optional components (e.g. assimp/FBX): a separate opt-in vendor entry with its
  own `builder`, or include `only` gating.
- Cleaning git vendors + build dirs via the `clean` stage.
- A `binary`-style metadata model for cmake args later (schema/validation).
