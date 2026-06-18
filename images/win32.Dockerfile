FROM debian:trixie-slim

RUN apt-get update && apt-get install -y \
    g++-mingw-w64-x86-64 \
    ca-certificates \
    curl \
    unzip \
    make \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-linux.tar.gz \
    | tar xz -C /usr/local/bin/ \
    && chmod +x /usr/local/bin/premake5

# Cache layer: mingw curl SDK (rarely changes)
RUN curl -fsSL https://curl.se/windows/dl-8.20.0_5/curl-8.20.0_5-win64-mingw.zip -o /tmp/curl-mingw.zip \
    && echo "d290b4ee475968b04dcb2e0c4f046f2ff25c35c65d3e54259cef7cc6cea069e2  /tmp/curl-mingw.zip" | sha256sum -c - \
    && unzip /tmp/curl-mingw.zip -d /tmp/curl-mingw \
    && cp -r /tmp/curl-mingw/curl-8.20.0_5-win64-mingw/include/* /usr/x86_64-w64-mingw32/include/ \
    && cp -r /tmp/curl-mingw/curl-8.20.0_5-win64-mingw/lib/*.a /usr/x86_64-w64-mingw32/lib/ \
    && mkdir -p /curl-dll \
    && cp /tmp/curl-mingw/curl-8.20.0_5-win64-mingw/bin/libcurl-x64.dll /curl-dll/ \
    && rm -rf /tmp/curl-mingw*

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
RUN ln -sf /usr/bin/x86_64-w64-mingw32-strip /usr/local/bin/strip \
    && cd predep \
    && premake5 gmake --os=windows --cc=gcc
RUN cd predep && \
    make -j$(nproc) config=release \
    CC=x86_64-w64-mingw32-gcc \
    CXX=x86_64-w64-mingw32-g++ \
    CXXFLAGS="-I/usr/x86_64-w64-mingw32/include" \
    LDFLAGS="-L/usr/x86_64-w64-mingw32/lib -static-libgcc -static-libstdc++ -Wl,--defsym,fstat64=_fstat64" \
    LIBS="-lcurl -lssl -lcrypto -lws2_32 -lwldap32 -lcrypt32 -lbcrypt -lz -lzstd -lpsl -lbrotlidec -lbrotlicommon -lnghttp2 -lnghttp3 -lngtcp2 -lngtcp2_crypto_libressl -lssh2" \
    && cp /curl-dll/libcurl-x64.dll bin/
