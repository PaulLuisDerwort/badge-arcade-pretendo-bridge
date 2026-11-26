#!/usr/bin/env bash
# Tests the installer's builder and SpotPass code on a PC, inside a
# badge-arcade-offline checkout (pinned commit + bridge.patch):
#   - upstream's own builder and SpotPass tests, run against our builder sources
#   - test_expanded.py: native Expanded == upstream's Python expand_catalog()
#   - test_spotpass.py: our console decryption == upstream's open_spotpass()
# No game data is used; tests that need private files are skipped.
#
# Needs git, python3 (3.11+), a C++17 compiler, clang (ARM) and OpenSSL headers.
set -euo pipefail

UPSTREAM=https://github.com/GrouchoGlasses42/badge-arcade-offline
COMMIT=b2281756b36f4bbbbee45d081098ac79072ccfac   # v0.5.4

repo="$PWD"
work="${RUNNER_TEMP:-/tmp}/nba-test"
if [ ! -d "$work/.git" ]; then
  git clone -q "$UPSTREAM" "$work"
fi
git -C "$work" checkout -q --force "$COMMIT"
git -C "$work" clean -qfdx
git -C "$work" apply "$repo/bridge.patch"

cp "$repo"/installer/source/builder/*.cpp "$repo"/installer/source/builder/nba.hpp "$work/src/weekly_native/"
cp "$repo"/installer/tools/host/host.cpp "$work/src/weekly_native/host.cpp"
cp "$repo"/installer/tools/host/test_expanded.py "$repo"/installer/tools/host/test_spotpass.py "$work/tests/"

host="$work/spotpass-host"
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  -I "$repo/installer/tools/host/fake3ds" -I "$repo/installer/source" \
  -I "$repo/installer/source/builder" -I "$repo/installer/source/builder/vendor" \
  "$repo/installer/source/spotpass.cpp" "$repo/installer/tools/host/spotpass_host.cpp" \
  "$repo"/installer/source/builder/files.cpp "$repo"/installer/source/builder/archive.cpp \
  "$repo"/installer/source/builder/vendor/tinyxml2.cpp -lcrypto -o "$host"

python3 -m pip install -q -r "$work/requirements-dev.txt" pycryptodome
cd "$work"
SPOTPASS_HOST="$host" PYTHONPATH=src:tests python3 -m pytest -q --tb=short \
  tests/test_weekly_builder.py tests/test_badge_spotpass.py tests/test_badge_catalog.py \
  tests/test_expanded.py tests/test_spotpass.py
