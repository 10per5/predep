---
title: Dependencies
weight: 11
---

# Dependencies

Download stages — fetch a remote archive or file, verify its SHA256, and
extract to a standard location.

## Vendor / Resource

| Type | Default destination |
| ---- | ------------------- |
| `vendor` | `root://vendor/` |
| `resource` | `root://resources/` |

```toml
[[stages]]
name = "sdl"
type = "vendor"
url = "https://example.com/sdl-${VERSION}.tar.gz"
vars = { version = "2.30" }
dest = "root://vendor/"
sha256 = "abc123..."
```

Dependencies declared at the root level via `[[vendor]]` or `[[resource]]`
arrays merge automatically into stages of matching type.

### When to use
- **vendor** — third-party libraries checked into the project tree
- **resource** — static assets bundled with the project (icons, templates)

## Fetch

A unified download stage. Use `fetch-type` to choose the kind of asset:

```toml
[[stages]]
name = "get-tools"
type = "fetch"
fetch-type = "binary"      # merges [[binary]] entries

[[stages]]
name = "get-specific"
type = "fetch"
fetch-type = "binary"
assets = ["tool-a", "tool-b"]  # only fetch these named entries
```

Root-level `[[binary]]` entries merge into fetch stages with
`fetch-type = "binary"`. Root-level `[[fetch]]` entries carry their own
`fetch-type` and merge into matching stages.

### When to use
- A single stage managing multiple download entries of the same kind
- Grouping all binary tool downloads under one stage
