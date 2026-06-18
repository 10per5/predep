FROM debian:trixie-slim AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    libcurl4-openssl-dev \
    libssl-dev \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-linux.tar.gz \
    | tar xz -C /usr/local/bin/ \
    && chmod +x /usr/local/bin/premake5

WORKDIR /src

# Cache layer: vendor deps (rarely change)
RUN mkdir -p /src/predep/vendor/ /src/predep/vendor/CLI11 && \
    curl -fsSL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz \
    | tar xz -C /tmp && \
    echo "6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e  /tmp/tomlplusplus-3.4.0/toml.hpp" \
    | sha256sum -c - && \
    mv /tmp/tomlplusplus-3.4.0 /src/predep/vendor/tomlpp && \
    curl -fsSL -o /src/predep/vendor/CLI11/CLI11.hpp \
    https://github.com/CLIUtils/CLI11/releases/download/v2.6.2/CLI11.hpp

# Cache layer: build config (only busted when premake5.lua or predep.toml changes)
COPY premake5.lua predep.toml ./predep/
COPY src ./predep/src/
RUN cd predep && premake5 gmake
RUN cd predep && make -j$(nproc) config=release
