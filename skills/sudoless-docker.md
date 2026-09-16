# Skill: Running predep without root (sudoless) in Docker

## What it is

`predep` refuses to run as `root` and prints:

```
ERROR: Do not run predep as root.
Use a normal user account. If elevated privileges are needed, predep will
prompt for sudo automatically during install/uninstall stages.
```

It only self-`sudo`s for the `install`/`uninstall` stages. The common `predep` invocation (`main` = `build` → `vendor` + `premake5`/`cmake`) never needs root, so it can run as a normal user inside a container.

## When to use

Any `Dockerfile` that runs `predep` to vendor + build. Apply this instead of running as root. (There is a `--privileged` flag that disables the guard, but prefer the sudoless approach — running as root in a build is unnecessary and weaker.)

## How

Create a normal build user, give it ownership of the build context, switch to it, then run `predep`. Based on the `rana-socket` `Dockerfile`:

```dockerfile
# builder base must satisfy the predep binary's glibc requirement (see Gotchas)
FROM debian:trixie AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++ cmake git curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ... install premake5, predep (binary), flatc as root ...

WORKDIR /src
COPY . .

# predep refuses root: run it as a normal user. It only self-sudo's for
# install/uninstall stages, which the build stage does not use.
RUN useradd -m -s /bin/bash bldr && chown -R bldr /src
USER bldr
ENV HOME=/home/bldr

RUN predep            # main stage = "build" -> vendor (pull) + premake5 (build)
```

## Gotchas

* **Writable HOME/cache:** predep's `cache://` resolves under `$HOME`. Set `ENV HOME=/home/bldr` (or wherever) so the non-root user can write its cache.


* **Artifact readability:** build outputs are copied by later `COPY --from=builder` stages (running as root). Ensure build products are world-readable (the usual `0755`/`0644` from the toolchain is fine); if a later stage can't read them, `chmod` in the build or `chmod` after.


* **Don't use **`**--privileged**` to silence the guard — it just runs as root. The non-root user is the intended, least-privilege path.