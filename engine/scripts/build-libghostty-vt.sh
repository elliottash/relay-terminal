#!/usr/bin/env bash
# Build the pinned libghostty-vt into a prefix for -DRELAY_ENGINE_WITH_GHOSTTY=ON.
#
#   engine/scripts/build-libghostty-vt.sh PREFIX [ZIG]
#
# Needs git, network access and Zig 0.16.x (https://ziglang.org/download/;
# pass its path as ZIG or put `zig` on PATH; engine/scripts/install-zig.sh fetches the
# pinned release). Produces PREFIX/include/ghostty/vt.h and PREFIX/lib/libghostty-vt.a.
# Then configure Relay with
#   -DRELAY_BUILD_ENGINE=ON -DRELAY_ENGINE_WITH_GHOSTTY=ON -DRELAY_GHOSTTY_VT_PREFIX=PREFIX
#
# A PREFIX that already holds this commit built with this Zig for this CPU is left alone
# (PREFIX/.relay-libghostty-vt records what was built); delete that file to force a rebuild.
#
# Environment:
#   RELAY_GHOSTTY_ZIG_CPU  Zig's -Dcpu= (default: native, the CPU of this machine).
#                          The packages use "baseline" so the archive runs on every CPU
#                          of the architecture; simdutf and Highway dispatch at run time.
#   GHOSTTY_SRC_DIR        where ghostty is cloned (default PREFIX/src/ghostty).
#
# libghostty-vt's C API is explicitly unstable. Bump GHOSTTY_COMMIT only together
# with engine/core/GhosttyCore.cpp (the only file that calls it) and re-run
# relay-engine-tests and relay-engine-bench. packaging/deb/build-deb.sh builds this
# commit into every .deb, so a bump changes the packages too.
set -euo pipefail

GHOSTTY_REPO=https://github.com/ghostty-org/ghostty.git
GHOSTTY_COMMIT=f9a3f24a56bf05f70894e1a084809d4fffadf420 # main, 2026-09-16
ZIG_VERSION_WANTED=0.16

prefix=${1:?usage: build-libghostty-vt.sh PREFIX [ZIG]}
zig=${2:-zig}
prefix=$(mkdir -p "$prefix" && cd "$prefix" && pwd)

zig_version=$("$zig" version 2>&1 || true)
if ! grep -q "^${ZIG_VERSION_WANTED}\." <<<"$zig_version"; then
    echo "error: need Zig ${ZIG_VERSION_WANTED}.x, found: $zig_version" >&2
    exit 1
fi

cpu=${RELAY_GHOSTTY_ZIG_CPU:-native}
stamp="ghostty=$GHOSTTY_COMMIT zig=$zig_version cpu=$cpu"
if [ -f "$prefix/include/ghostty/vt.h" ] && [ -f "$prefix/lib/libghostty-vt.a" ] &&
   [ "$(cat "$prefix/.relay-libghostty-vt" 2>/dev/null)" = "$stamp" ]; then
    echo "libghostty-vt $GHOSTTY_COMMIT already in $prefix ($stamp)"
    exit 0
fi
rm -f "$prefix/.relay-libghostty-vt"

src=${GHOSTTY_SRC_DIR:-$prefix/src/ghostty}
if [ ! -d "$src/.git" ]; then
    git clone --filter=blob:none "$GHOSTTY_REPO" "$src"
fi
git -C "$src" fetch --quiet origin "$GHOSTTY_COMMIT" || true
git -C "$src" checkout --quiet "$GHOSTTY_COMMIT"

cd "$src"
"$zig" build -Demit-lib-vt -Doptimize=ReleaseFast -Dcpu="$cpu" --prefix "$prefix"
test -f "$prefix/include/ghostty/vt.h"
test -f "$prefix/lib/libghostty-vt.a"
printf '%s\n' "$stamp" > "$prefix/.relay-libghostty-vt"
echo "libghostty-vt $GHOSTTY_COMMIT installed in $prefix ($stamp)"
