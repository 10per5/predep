# Data Model Specification

## 1. Enums

### `platform_type` (`src/data/const.h`)

```cpp
enum class platform_type { linux, darwin, windows };

platform_type current_platform();                    // runtime detection
platform_type platform_from_string(const std::string &);
std::string to_string(platform_type);
```

Used as map key for all platform-override lookups. Replaces `std::string` throughout.

### `stage_type` (`src/data/stage.h`)

```cpp
enum class stage_type {
    vendor, fetch, resource,   // download family
    run, docker, premake5,     // buildable family
    package, group,
    disabled, binary           // execution family
};

stage_type stage_from_string(const std::string &);
std::string to_string(stage_type);
```

The `type` field on `stage_desc`. Resolver maps this to registered action handlers.

---

## 2. Type-inheritance tree

```
stage_data                         ← dispatch vtable only
├── download_data                  ← fetch-type, assets, entries + stage vars
├── buildable_data                 ← outputs, build_context (shared by buildables)
│   ├── run_data                   ← commands
│   ├── binary_data                ← binary name, args
│   ├── docker_data                ← recipe, target, dest
│   └── premake5_data              ← action, make, strip, target, project
├── package_data                   ← artifacts, bundle
└── group_data                     ← no extra fields
```

---

## 3. Entry structs (same type for defaults and platform overrides)

```cpp
struct run_entry {
    std::vector<std::string> commands;
};

struct docker_entry {
    std::string recipe;
    std::string target;
    std::string dest;
};

struct premake5_entry {
    std::string action = "gmake";
    std::optional<bool> make = true;     // always set in defaults; nullopt in platform = skip
    std::optional<bool> strip = true;    // same
    std::string target;
    std::string project;
};
```

### Entry structs

```cpp
struct binary_entry {
    std::map<std::string, std::string> params;  // --key value pairs (DANGEROUS)
    std::vector<std::string> args;               // free-form argv (DANGEROUS)
};
```

### Platform entry template (adds `build_context` override to any entry type)

```cpp
template<typename T>
struct platform_entry : T {
    std::string build_context;           // empty = not set → use buildable_data default
};
```

For run, a `platform_entry<run_entry>` inherits `commands` from `run_entry` and adds `build_context`.
For premake5, a `platform_entry<premake5_entry>` inherits `action` / `optional<bool> make` / etc. and adds `build_context`.

---

## 4. Data structs (polymorphic)

```cpp
struct fetch_entry {
    std::string fetch_type;                    // "vendor"|"binary"|"resource" — stamped by merge
    std::string name;
    std::string url;
    std::string dest;
    std::string sha256;
    std::string output_name;
    bool extract = false;
    bool create_directory = false;
    std::map<std::string, std::string> vars;
};

struct download_data : stage_data {
    std::string fetch_type;                    // stage-level filter (for type="fetch" stages)
    std::vector<std::string> assets;           // stage-level name filter
    std::vector<fetch_entry> entries;          // all download entries (merged from root arrays)
    std::map<std::string, std::string> vars;   // stage-level vars (interpolation)
};

struct buildable_data : stage_data {
    std::vector<std::string> outputs;
    std::string build_context;
};

struct run_data : buildable_data {
    run_entry defaults;
    std::map<platform_type, platform_entry<run_entry>> platform;
};

struct docker_data : buildable_data {
    docker_entry defaults;
    std::map<platform_type, platform_entry<docker_entry>> platform;
};

struct premake5_data : buildable_data {
    premake5_entry defaults;
    std::map<platform_type, platform_entry<premake5_entry>> platform;
};

struct package_data : stage_data {
    std::vector<artifact_entry> artifacts;
    std::string bundle;
};

struct group_data : stage_data {};
```

---

## 5. Stage descriptor

```cpp
struct stage_desc {
    std::string name;
    std::string description;
    stage_type type;
    std::vector<std::string> depends;
    std::string config_dir;                    // parser-internal: resolver overrides ctx.root with this
    std::unique_ptr<stage_data> data;
};
```

Comparison with current `stage_desc`:

| Removed              | Where it moved                         |
|----------------------|----------------------------------------|
| `outputs`            | `buildable_data`                       |
| `build_context`      | `buildable_data`                       |
| `platforms`          | replaced by per-type `platform` map    |
| `vars`               | `download_data.vars`                   |

`config_dir` kept — resolver needs it to override `ctx.root` per-stage for correct path resolution on included manifests.

---

## 6. Resolution logic (per action type)

Each action's `resolve(stage_desc &sd, ...)`:

1. `dynamic_cast` `sd.data` to concrete type (e.g., `run_data*`)
2. Start with `d->defaults`
3. Look up `d->platform.find(ctx.platform)`
4. If found, merge fields:
   - strings: `if !entry.field.empty() → override`
   - optionals: `if entry.field.has_value() → override`
   - vectors: `if !entry.commands.empty() → override`
5. Resolve `build_context`:
   ```
   bc = d->build_context
   if platform found && !entry.build_context.empty()
       bc = entry.build_context
   ```
6. Resolve working directory: `auto cwd = action::resolve_cwd(bc, ctx);`
7. Call `security::confirm_build_context(sd, bc, cwd, ctx, error)`

---

## 7. Root-level array merging

`[[vendor]]`, `[[binary]]`, `[[resource]]`, `[[fetch]]` at the root level merge into `download_data::entries` on stages of matching type. Each entry is stamped with its `fetch_type` upon merge. `[[vendor]]` and `[[resource]]` merge into `type = "vendor"`/`"resource"` stages (exact match). `[[binary]]` (backward compat) and `[[fetch]]` merge into `type = "fetch"` stages filtered by `fetch-type` and `assets`.

---

## 8. vars exclusively on download types

Stage-level `[vars]` parsed only for `vendor`/`fetch`/`resource`. Stored in `download_data::vars`.  
Runtime vars (`PLATFORM`, `ARCH`, `CPU`, `OS`, `EXE_SUFFIX`) remain globally available.

---

## 9. Config mapping (TOML → data)

| TOML                          | C++ target                                  |
|-------------------------------|---------------------------------------------|
| `main`                        | `engine::impl::main_stage_name`             |
| `project`                     | `engine::impl::project` + `premake5_entry::project` |
| `[[stages]] name/type/depends` | `stage_desc`                                |
| `outputs`                     | `buildable_data::outputs`                   |
| `build_context`               | `buildable_data::build_context`             |
| `[platform.<os>]` fields      | per-type `platform_entry<T>`                |
| `build_context` in platform   | `platform_entry<T>::build_context`          |
| `commands`                    | `run_entry::commands`                       |
| `recipe`/`target`/`dest`      | `docker_entry` fields                       |
| `action`/`make`/`strip`/`project` | `premake5_entry` fields                 |
| `[vars]`                      | `download_data::vars`                       |
| `[[vendor/binary/resource/fetch]]` | `fetch_entry` → `download_data::entries` |
| `artifacts`/`bundle`          | `package_data`                              |
