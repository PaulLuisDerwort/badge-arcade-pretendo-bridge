#!/usr/bin/env bash
# Builds the installer app (.cia and .3dsx) with all six mode patches embedded.
#
# Meant for the devkitpro/devkitarm image, which has devkitARM and libctru but
# not the CIA tools: makerom and bannertool are downloaded (pinned versions,
# checked against SHA-256) when they aren't on the PATH. Versions and hashes
# are the ones used by Badge Arcade Reclawed's build.
#
# Usage: .github/scripts/build_installer.sh   -> dist/pretendo-bridge-installer.{cia,3dsx}
set -euo pipefail

# makerom 0.18.4, not 0.19.0: the 0.19.0 Linux build needs glibc 2.38 and the
# devkitPro image is Debian bookworm (glibc 2.36).
MAKEROM_URL=https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-ubuntu_x86_64.zip
MAKEROM_SHA256=dd596854718c195c6e3229286be485b122921715555af8ae5cf8e9a465d9f970
BANNERTOOL_URL=https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip
BANNERTOOL_SHA256=e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed

tools="${RUNNER_TEMP:-/tmp}/ctr-tools"
mkdir -p "$tools"

# fetch <url> <sha256> <zip member> <name>: download, verify, extract one binary.
fetch() {
  local zip="$tools/$4.zip"
  curl -fsSL --retry 3 -o "$zip" "$1"
  echo "$2  $zip" | sha256sum -c -
  unzip -o -j -q "$zip" "$3" -d "$tools"
  chmod +x "$tools/$4"
}

command -v makerom >/dev/null || fetch "$MAKEROM_URL" "$MAKEROM_SHA256" makerom makerom
command -v bannertool >/dev/null || fetch "$BANNERTOOL_URL" "$BANNERTOOL_SHA256" linux-x86_64/bannertool bannertool
export PATH="$tools:$PATH"

# The patches the app writes to the SD card, checked against SHA256SUMS.
sha256sum -c SHA256SUMS
mkdir -p installer/data
cp ips/home-menu/code.ips installer/data/vanilla_home.ips
cp ips/game-only/code.ips installer/data/vanilla_game.ips
cp ips/expanded/home-menu.ips installer/data/expanded_home.ips
cp ips/expanded/game-only.ips installer/data/expanded_game.ips
cp ips/weekly/home-menu.ips installer/data/weekly_home.ips
cp ips/weekly/game-only.ips installer/data/weekly_game.ips

make -C installer cia   # builds the .3dsx first, then packs the .cia
mkdir -p dist
cp installer/pretendo-bridge-installer.cia installer/pretendo-bridge-installer.3dsx dist/
(cd dist && sha256sum pretendo-bridge-installer.* > SHA256SUMS-installer)
ls -l dist
