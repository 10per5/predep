#!/usr/bin/env bash
#
# setup-hooks.sh — point this repo's git at the local git-hooks/ directory so
# hooks (e.g. pre-push) run without copying them into .git/hooks.
#
# Usage:  scripts/setup-hooks.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOOKS_DIR="$REPO_ROOT/git-hooks"

if [ ! -d "$HOOKS_DIR" ]; then
    echo "Error: hooks directory not found: $HOOKS_DIR" >&2
    exit 1
fi

git -C "$REPO_ROOT" config core.hooksPath "$HOOKS_DIR"

echo "Git hooks path set to: $HOOKS_DIR"
echo "The pre-push hook now enforces 'build(<scope>): <message>' on main."
