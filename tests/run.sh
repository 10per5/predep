#!/usr/bin/env bash
# predep integration tests
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREDEP_DIR="$SCRIPT_DIR/.."
BINARY="$PREDEP_DIR/bin/predep"
PASS=0
FAIL=0

pass()  { echo "  ✓ $1"; PASS=$((PASS + 1)); }
fail()  { echo "  ✗ $1"; FAIL=$((FAIL + 1)); }

check() {
    local desc="$1" cmd="$2" expect="$3"
    local out
    out="$($cmd 2>&1 || true)"
    if echo "$out" | grep -q "$expect"; then
        pass "$desc"
    else
        fail "$desc"
        echo "    expected: $expect"
        echo "    got: $out"
    fi
}

skip() {
    echo "  ~ $1 (skipped)"
}

echo ""
echo "  [predep tests]"
echo ""

if [ ! -f "$BINARY" ]; then
    echo "  Binary not found at $BINARY — see predep/README.md for build instructions"
    exit 1
fi

# Basic CLI
check "help flag" \
    "$BINARY --help" \
    "Usage"

check "list stages from config" \
    "$BINARY --list" \
    "hugo-binary"

check "unknown stage" \
    "$BINARY nonexistent 2>&1 || true" \
    "unknown stage"

check "unknown option" \
    "$BINARY --bogus 2>&1 || true" \
    "unknown option"

# Config detection
check "config not found error" \
    "$BINARY --config /nonexistent/predep.toml 2>&1 || true" \
    "failed to load config"

# Package (will fail because no editor assets — but should reach the right error)
check "package with missing gui" \
    "$BINARY package 2>&1 || true" \
    "assembly"

echo ""
echo "  Results: $PASS passed, $FAIL failed"
echo ""

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
