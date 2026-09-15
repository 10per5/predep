# Skill: Dependency trees in predep

## What it is

predep is a **DAG resolver**. Each `[[stages]]` entry declares `depends`, and
predep topologically orders stages so every dependency resolves before its
consumers. The engine also composes multiple manifests via `[[include]]`, and
auto-wires some links (notably the vendor `builder` link). This skill covers how
to express and reason about dependency trees.

## The core: `depends`

```toml
[[stages]]
name = "vendor"
type = "vendor"

[[stages]]
name = "build"
type = "premake5"
depends = ["vendor"]      # runs after vendor resolves
outputs = ["root://bin/myapp"]

[[stages]]
name = "package"
type = "package"
depends = ["build"]       # runs after build

[[stages]]
name = "all"
type = "group"
depends = ["package"]     # single target that pulls the whole chain
```

Resolving any stage pulls its **transitive** dependencies in order:
`predep all` → vendor → build → package. `predep build` → vendor → build.

## Including manifests (namespacing)

Split a large project into sub-manifests and compose them. Each include gets a
namespace (default: the including file's directory name). Stages become
`namespace::name`.

```toml
# root predep.toml
main = "all"
project = "myapp"

[[include]]
path = "engine/predep.toml"      # namespace defaults to "engine"

[[include]]
path = "gui/predep.toml"
namespace = "gui"               # explicit namespace

[[include]]
path = "tools/partial.toml"
only = ["lint", "format"]       # import only these stages

[[stages]]
name = "all"
type = "group"
depends = ["engine::build", "gui::build"]
```

| Field | Description |
| ----- | ----------- |
| `path` | Path to the included manifest (relative to including file) |
| `namespace` | Prefix (default: including file's directory name) |
| `only` | Import only the listed stage names |

Internal `depends` within an included file are automatically rewritten with the
prefixed name, so the sub-manifest stays self-contained.

## The `builder` auto-link

A `[[vendor]]` entry with `builder = "stageName"` links to a build stage. predep
ensures that build stage depends on the `vendor` stage automatically — you don't
repeat `depends = ["vendor"]` on the builder. (Covered in `vendor-libraries.md`
and `vendor-git-ref.md`.)

```toml
[[vendor]]
name = "fmt"
url = "https://.../fmt-11.0.2.tar.gz"
dest = "root://vendor/fmt/"
create_directory = true
builder = "fmt-cmake"          # fmt-cmake implicitly depends on "vendor"

[[stages]]
name = "fmt-cmake"
type = "cmake"
source = "root://vendor/fmt"
```

## Cross-manifest and cross-stage references

- Reference any stage as `namespace::name` in `depends`.
- The root manifest's `main` is the default target when `predep` runs with no
  stage argument.
- A `group` stage is a dependency-only alias (no-op at resolve time) — ideal for
  the `main` target that knits subtrees together.

## Gotchas

- **Cycles fail** — predep errors on a dependency cycle. Keep the graph acyclic.
- **Missing stage in `depends`** errors at resolution time, not silently.
- **`only` can drop a needed dep** — if you `only = ["build"]` but `build`
  `depends` on an omitted stage, resolution fails. Import the full subtree or
  adjust `depends`.
- **`clean` is not a dependency** — the `clean` stage accepts `targets`/`paths`
  to remove artifacts but does not resolve those targets as build deps; it just
  deletes them.
