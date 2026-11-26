#!/usr/bin/env bash
# Rebuilds every IPS in ips/ from badge-arcade-offline (pinned commit) plus
# bridge.patch, and fails if the result differs from what is committed.
#
# Needs git, python3 (3.11+) and clang with ARM support.
# Usage: .github/scripts/build_mode_ips.sh [--write]
set -euo pipefail

UPSTREAM=https://github.com/GrouchoGlasses42/badge-arcade-offline
COMMIT=b2281756b36f4bbbbee45d081098ac79072ccfac   # v0.5.4

work="${RUNNER_TEMP:-/tmp}/nba-upstream"
if [ ! -d "$work/.git" ]; then
  git clone -q "$UPSTREAM" "$work"
fi
git -C "$work" checkout -q --force "$COMMIT"
git -C "$work" clean -qfdx
git -C "$work" apply "$PWD/bridge.patch"

out="${RUNNER_TEMP:-/tmp}/nba-ips"
mkdir -p "$out"
PYTHONPATH="$work/src" python3 tools/build_mode_ips.py --out "$out"

if [ "${1:-}" = "--write" ]; then
  cp -r "$out"/. ips/
  echo "ips/ updated."
else
  diff -r "$out" ips && echo "ips/ matches the source."
fi
