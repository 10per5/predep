---
title: Bundling & Install
weight: 14
---

# Bundling & Install

## Package

Copies artifacts into a dist directory and creates a compressed archive:

```toml
[[stages]]
name = "package"
type = "package"
bundle = "myapp"
depends = ["all"]
artifacts = [
    { source = "root://bin/myapp", dest = "bin/" },
    { source = "root://public/", dest = "public" },
]
```

Setting `binary = true` on an artifact appends `.exe` on Windows:

```toml
artifacts = [
    { source = "root://gui/bin/app", dest = "app", binary = true },
]
```

Produces `myapp.tar.gz` (Unix) or `myapp.zip` (Windows).

### When to use
- Assembling a release distribution
- Creating deployable archives with a consistent layout
- Bundling multiple build outputs into a single artifact

## Install / Uninstall

Auto-generated from the root `[install]` table (or written manually as
`[[stages]]`). Copy artifacts to a system prefix and remove them.

```toml
[install]
depends = ["build"]
dir = "/opt/myapp"
symlink = true
artifacts = [
    { source = "root://bin/myapp", dest = "myapp" },
]
```

Default install directory: `/usr/local/bin` (Unix) or
`C:/Program Files/<project>` (Windows). Falls back to `sudo` automatically
when the destination requires elevated privileges.

### Per-artifact permissions

`chmod` (octal mode) and `chown_user` (bool) can be set per artifact, or as
root-level `[install]` defaults applied to artifacts that don't set their own:

```toml
[install]
depends = ["build"]
chmod = 644
artifacts = [
    { source = "root://bin/myapp", dest = "myapp", chmod = 755, chown_user = true },
]
```

`chmod` values are octal — `755` means `0o755`, and strings like `"0644"`
are accepted. `chown_user` gives ownership of the installed file to the
invoking user, so later re-installs can overwrite it without sudo. Both fall
back to `sudo chmod` / `sudo chown` when elevation is needed (see
[security.md](../security.md) for the sudo escalation model). Ignored on
Windows.

See [security.md](../security.md) for the sudo escalation model.

### When to use
- Installing build artifacts system-wide
- Making tools available in PATH
- Creating symlinks to the first artifact
