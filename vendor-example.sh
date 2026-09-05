#!/usr/bin/env bash
# tools/3dcpp/vendor/fetch.sh
#
# Vendor C++ dependencies at pinned commits, then build Magnum + Corrade
# into vendor/prefix (consumed by premake5.lua). Assimp is optional for FBX.
#
# Run from this directory: ./fetch.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$ROOT/prefix"
mkdir -p "$PREFIX"

# ── Pinned versions (Exact Commit SHAs / Stable Tag) ─────────────────────────
CORRADE_REF="c028fd6e261a0665f72fbc75c7c034baf47747c6" # Target Corrade SHA
MAGNUM_REF="f239823e9afd27ab0ecf28e1d567831e3c62a25e"  # Target Magnum SHA
ASSIMP_REF="v5.4.3"                                    # Release Tag

# ── Helper to clone/checkout exact SHAs or tags ───────────────────────────────
fetch_repo() {
    local dir="$1"
    local url="$2"
    local ref="$3"

    # Only skip if directory exists AND actually contains source code
    if [ ! -f "$dir/CMakeLists.txt" ]; then
        echo "[fetch] Fetching $dir ($ref)..."
        rm -rf "$dir"

        # Clone master shallowly, then checkout the target commit/tag
        git clone --depth 50 --single-branch "$url" "$dir"
        git -C "$dir" checkout "$ref"
    else
        echo "[fetch] Directory $dir exists and contains code, skipping fetch."
    fi
}

# ── Fetch dependencies ────────────────────────────────────────────────────────
fetch_repo "corrade" "https://github.com/mosra/corrade.git" "$CORRADE_REF"
fetch_repo "magnum" "https://github.com/mosra/magnum.git" "$MAGNUM_REF"

# FBX support (uncomment fetch + build steps below if required):
# fetch_repo "assimp"  "https://github.com/assimp/assimp.git"  "$ASSIMP_REF"

# ── Build Corrade → prefix ────────────────────────────────────────────────────
echo "[fetch] Building Corrade..."
cmake -S corrade -B build-corrade \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build build-corrade -j"$(nproc)"
cmake --install build-corrade

# ── Build Magnum → prefix (Vulkan backend + needed plugins) ───────────────────
echo "[fetch] Building Magnum..."
cmake -S magnum -B build-magnum \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DMAGNUM_TARGET_VK=ON \
    -DMAGNUM_BUILD_VK=ON \
    -DMAGNUM_WITH_VULKANAPPLICATION=ON \
    -DMAGNUM_WITH_GLTFIMPORTER=ON \
    -DMAGNUM_WITH_ASSIMPIMPORTER=ON \
    -DMAGNUM_WITH_STBIMAGEIMPORTER=ON \
    -DMAGNUM_WITH_STBIMAGECONVERTER=ON \
    -DMAGNUM_WITH_TRADE=ON \
    -DCORRADE_ROOT="$PREFIX"
cmake --build build-magnum -j"$(nproc)"
cmake --install build-magnum

# ── Optional FBX: build Assimp → prefix ───────────────────────────────────────
# echo "[fetch] Building Assimp..."
# cmake -S assimp -B build-assimp \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_INSTALL_PREFIX="$PREFIX" \
#     -DASSIMP_BUILD_TESTS=OFF \
#     -DBUILD_SHARED_LIBS=OFF \
#     -DCMAKE_POSITION_INDEPENDENT_CODE=ON
# cmake --build build-assimp -j"$(nproc)"
# cmake --install build-assimp

echo "[fetch] Vendored deps installed successfully to: $PREFIX"
