---
title: Sandboxing — Windows
weight: 56
---

# Windows Sandbox Plan (Future)

> This document describes the planned Windows sandbox integration (AppContainer).
> Linux sandboxing (bwrap/landlock/seccomp) is documented in `docs/security/sandbox.md`.

## Backend: AppContainer (Windows 8+)

Uses the Win32 `AppContainer` API — the same sandbox UWP apps use. No
external tool, pure Win32 API calls through `CreateAppContainerToken` +
`CreateProcessAsUser`.

```cpp
// 1. Create a derived AppContainer SID from our identifier
PSID sid = nullptr;
DeriveAppContainerSidFromAppContainerName(
    L"predep.sandbox.<stage>", &sid);

// 2. Set up capabilities based on profile
SID_AND_ATTRIBUTES caps[] = {};
int n = 0;
if (profile.network == "full") {
    caps[n++] = { WinBuiltinCapabilitySid(internetClient), ... };
} else if (profile.network == "loop") {
    caps[n++] = { WinBuiltinCapabilitySid(privateNetworkClientServer), ... };
}

// 3. Create sandbox token
HANDLE hSandboxToken;
CreateAppContainerToken(hToken, sid, caps, n, &hSandboxToken);

// 4. Grant access to allowed paths
// Filesystem: AppContainer can only access its package directory
// by default. Add grants for read_only / writable paths.
AddPackageSidToDirectoryAcl(sid, read_only_path, FILE_GENERIC_READ);
AddPackageSidToDirectoryAcl(sid, writable_paths,
    FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE);

// 5. Spawn process inside the container
CreateProcessAsUser(hSandboxToken, binary, args, ...);
```

## Key properties

- **No external binary** — pure Win32, always available on Win8+
- **Filesystem isolation** — container gets its own package root at
  `%USERPROFILE%\AppData\Local\Packages\predep.sandbox.<stage>\`;
  all other paths must be explicitly granted via ACL modification
- **Network isolation** — capability-based: `internetClient` (outbound),
  `internetClientServer` (bidirectional), `privateNetworkClientServer` (LAN)
- **Process identity** — runs as a unique SID, cannot interact with
  processes outside the container
- **Registry virtualization** — separate registry view
- **No PID namespace** — can still enumerate other processes
- **No capability dropping** — Windows doesn't have POSIX capabilities
- **Heavy setup** — each sandbox identity requires creating a package root
  and ACL modifications on every allowed path

## Profile field compatibility

The same `binaries/*.toml` profile files work across platforms. The
AppContainer backend maps each field as follows:

| Profile field | AppContainer implementation |
|---------------|-----------------------------|
| `read_only`   | Grant `FILE_GENERIC_READ` via `AddPackageSidToDirectoryAcl` |
| `writable`    | Grant `FILE_GENERIC_READ \| FILE_GENERIC_WRITE \| DELETE` |
| `hidden`      | Implicit — AppContainer denies all paths by default; omitted paths are hidden |
| `network = "none"` | Omit all network capabilities |
| `network = "loop"` | Grant `privateNetworkClientServer` only |
| `network = "full"` | Grant `internetClient` |
| `caps = "none"` | N/A — AppContainer inherently has no special privileges |
| `caps = "keep"` | N/A |
| IPC / PID / UTS | Not available |

The universal-safe default maps to:

```
read_only = ["root://"]
writable  = ["cache://"]
network = "none"
```

Translated to AppContainer: `FILE_GENERIC_READ` on project root,
`GENERIC_READ | GENERIC_WRITE | DELETE` on cache, no network capabilities.

## Build system

```lua
filter("system:windows")
if _OPTIONS["sandbox"] then
    defines({ "WITH_SANDBOX" })
    files({ "src/sys/appcontainer.cpp" })
end
```

On non-Windows platforms, the AppContainer backend is not compiled.

## Implementation order (Windows)

1. `src/sys/appcontainer.h/cpp` — `DeriveAppContainerSidFromAppContainerName`
   + `CreateAppContainerToken` + `CreateProcessAsUser`
2. Path ACL grant helpers (`AddPackageSidToDirectoryAcl` wrappers)
3. Capability mapping: `network` enum → AppContainer capability SIDs
4. Integration with `binary_action::resolve()` — runs after profile merge,
   before `exec`/`CreateProcess`
5. Detection order: `appcontainer` → no sandbox (fallback with warning)
