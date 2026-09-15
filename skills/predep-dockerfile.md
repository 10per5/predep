# Skill: Using predep in Dockerfiles

## What it is

`predep` is a declarative stage processor driven by a `predep.toml` manifest. Each project declares `[[stages]]` (e.g. `vendor`, `premake5`, `build`). With no arguments `predep` resolves the stage named by `main`; `predep <stage>` resolves a single stage and its transitive `depends`.

Use it to replace hand-rolled `curl` vendor blocks and the `premake5 gmake && make` dance with a single `RUN predep` that vendors dependencies and then builds.

## Install the latest binary

A prebuilt linux binary is published per release as `predep-linux-x86_64.tar.gz`. Pull and install it the same way as premake5:

```dockerfile
RUN curl -fsSL "https://github.com/10per5/predep/releases/latest/download/predep-linux-x86_64.tar.gz" -o /tmp/predep.tgz \
    && mkdir -p /tmp/pd && tar -xzf /tmp/predep.tgz -C /tmp/pd \
    && find /tmp/pd -name predep -type f -exec install -m 0755 {} /usr/local/bin/predep \; \
    && rm -rf /tmp/pd /tmp/predep.tgz \
    && predep --version
```

The `find ... -name predep` step keeps the recipe independent of the archive's inner layout.

## Dockerfile pattern (rana-socket style)

```dockerfile
# syntax=docker/dockerfile:1
FROM debian:12@sha256:2f65600e1252c5649d2213e1d1ea4d74253d26514dc6530102a875e429245929 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++ cmake git curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# premake5 is required by predep's premake5 build stage.
ARG PREMAKE_VER=5.0.0-beta2
RUN curl -fsSL "https://github.com/premake/premake-core/releases/download/v${PREMAKE_VER}/premake-${PREMAKE_VER}-linux.tar.gz" -o /tmp/premake.tgz \
    && mkdir -p /tmp/pm && tar -xzf /tmp/premake.tgz -C /tmp/pm \
    && find /tmp/pm -name premake5 -type f -exec install -m 0755 {} /usr/local/bin/premake5 \; \
    && rm -rf /tmp/pm /tmp/premake.tgz

# predep — vendors deps + builds per predep.toml.
ARG PREDEP_VER=0.0.2
RUN curl -fsSL "https://github.com/10per5/predep/releases/download/v${PREDEP_VER}/predep-linux-x86_64.tar.gz" -o /tmp/predep.tgz \
    && mkdir -p /tmp/pd && tar -xzf /tmp/predep.tgz -C /tmp/pd \
    && find /tmp/pd -name predep -type f -exec install -m 0755 {} /usr/local/bin/predep \; \
    && rm -rf /tmp/pd /tmp/predep.tgz

# flatc — only needed if a premake prebuild generates FlatBuffers headers.
ARG FLATBUFFERS_VER=24.3.25
RUN curl -fsSL "https://github.com/google/flatbuffers/archive/refs/tags/v${FLATBUFFERS_VER}.tar.gz" -o /tmp/fb.tgz \
    && mkdir -p /tmp/fb && tar -xzf /tmp/fb.tgz -C /tmp/fb \
    && cmake -S "/tmp/fb/flatbuffers-${FLATBUFFERS_VER}" -B /tmp/fb/build \
        -DCMAKE_BUILD_TYPE=Release -DFLATBUFFERS_BUILD_TESTS=OFF -DFLATBUFFERS_BUILD_FLATC=ON \
    && cmake --build /tmp/fb/build -j"$(nproc)" --target flatc \
    && install -m 0755 /tmp/fb/build/flatc /usr/local/bin/flatc \
    && rm -rf /tmp/fb /tmp/fb.tgz

WORKDIR /src
COPY . .
RUN predep            # main stage = "build" -> vendor (pull) + premake5 (build)
```

## predep.toml essentials

```toml
main = "build"
project = "rana-socketd"

[[vendor]]
name = "tomlplusplus"
url  = "https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp"
sha256 = "6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e"  # optional
dest = "root://vendor/tomlplusplus/"   # MUST match premake include dirs
create_directory = true

[[vendor]]
name = "flatbuffers"
url  = "https://github.com/google/flatbuffers/archive/refs/tags/v24.3.25.tar.gz"
dest = "root://vendor/flatbuffers/"
create_directory = true     # tarballs auto-extract (.tar.gz / .zip)

[[stages]]
name = "vendor"
type = "vendor"

[[stages]]
name = "build"
type = "premake5"
depends = ["vendor"]
outputs = ["root://bin/Release/rana-socketd"]
```

* `root://` is the directory containing `predep.toml` (the build-context root).


* `dest` is relative to `root://`. A bare `root://vendor/` keeps the URL's filename (e.g. `vendor/toml.hpp`); `root://vendor/tomlplusplus/` nests it. **Make **`**dest**`** match what **`**premake5.lua**`** actually includes.**


* `sha256` is per-entry; omit it if unknown (verification is skipped).


* Archive entries auto-extract, preserving the tarball's own top-level dir (e.g. `flatbuffers-24.3.25/`) — so set `dest` to the parent directory.

## Gotchas

* The `premake5` stage type shells out to `premake5` + `make`, so both must be installed in the image.


* `predep` reads `predep.toml` from the working directory — ensure `COPY . .` includes it and `.dockerignore` does not exclude it.


* The #1 cause of "header not found" failures is a `vendor` `dest` that does not line up with the premake include path. Reconcile them.