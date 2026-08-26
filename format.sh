#!/usr/bin/env bash
set -euo pipefail

# ─── Configure these ──────────────────────────────────────────────────────────

DIRECTORIES=(
    include
    src
    tests
    tutorials
)

EXTENSIONS=(
    cpp
    hpp
    h
)

# ──────────────────────────────────────────────────────────────────────────────

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLANG_FORMAT_BIN="${CLANG_FORMAT:-clang-format}"

if ! command -v "$CLANG_FORMAT_BIN" >/dev/null 2>&1; then
    echo "error: clang-format executable not found: $CLANG_FORMAT_BIN" >&2
    exit 1
fi

for dir in "${DIRECTORIES[@]}"; do
    abs_dir="$REPO_ROOT/$dir"
    [[ -d "$abs_dir" ]] || continue
    for ext in "${EXTENSIONS[@]}"; do
        find "$abs_dir" -type f -name "*.$ext" -exec "$CLANG_FORMAT_BIN" -i {} +
    done
done
