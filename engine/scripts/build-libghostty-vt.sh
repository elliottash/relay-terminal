#!/usr/bin/env bash
# Build the pinned libghostty-vt into a prefix for -DRELAY_ENGINE_WITH_GHOSTTY=ON.
#
#   engine/scripts/build-libghostty-vt.sh PREFIX [ZIG]
#
# Needs git, network access and Zig 0.16.x (https://ziglang.org/download/;
# pass its path as ZIG or put `zig` on PATH). Produces PREFIX/include/ghostty/vt.h
# and PREFIX/lib/libghostty-vt.a. Then configure Relay with
#   -DRELAY_BUILD_ENGINE=ON -DRELAY_ENGINE_WITH_GHOSTTY=ON -DRELAY_GHOSTTY_VT_PREFIX=PREFIX
#
# libghostty-vt's C API is explicitly unstable. Bump GHOSTTY_COMMIT only together
# with engine/core/GhosttyCore.cpp (the only file that calls it) and re-run
# relay-engine-tests and relay-engine-bench.
set -euo pipefail

GHOSTTY_REPO=https://github.com/ghostty-org/ghostty.git
GHOSTTY_COMMIT=f9a3f24a56bf05f70894e1a084809d4fffadf420 # main, 2026-09-16
ZIG_VERSION_WANTED=0.16

prefix=${1:?usage: build-libghostty-vt.sh PREFIX [ZIG]}
zig=${2:-zig}
prefix=$(mkdir -p "$prefix" && cd "$prefix" && pwd)

if ! "$zig" version | grep -q "^${ZIG_VERSION_WANTED}\."; then
    echo "error: need Zig ${ZIG_VERSION_WANTED}.x, found: $("$zig" version 2>&1)" >&2
    exit 1
fi

src="$prefix/src/ghostty"
if [ ! -d "$src/.git" ]; then
    git clone --filter=blob:none "$GHOSTTY_REPO" "$src"
fi
git -C "$src" fetch --quiet origin "$GHOSTTY_COMMIT" || true
git -C "$src" checkout --quiet "$GHOSTTY_COMMIT"

cd "$src"
"$zig" build -Demit-lib-vt -Doptimize=ReleaseFast --prefix "$prefix"
test -f "$prefix/include/ghostty/vt.h"
test -f "$prefix/lib/libghostty-vt.a"
echo "libghostty-vt $GHOSTTY_COMMIT installed in $prefix"
