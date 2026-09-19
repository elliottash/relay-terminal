#!/usr/bin/env bash
# Download, verify and unpack the Zig release pinned in engine/scripts/zig.sha256 for this
# machine's architecture (the packaged builds use it for libghostty-vt).
#
#   engine/scripts/install-zig.sh DEST [CACHE_DIR]
#
# Prints the path of the zig executable, DEST/zig. The tarball comes from
# https://ziglang.org/download/<version>/ and must match zig.sha256 (taken from
# https://ziglang.org/download/index.json), or nothing is unpacked. CACHE_DIR keeps the
# tarball between runs; it is verified again every time. Needs curl, xz and tar.
#
# Bumping Zig: put the new release's x86_64 and aarch64 checksums from index.json in
# zig.sha256 (both lines, same version), and keep ZIG_VERSION_WANTED in
# build-libghostty-vt.sh in step.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
dest=${1:?usage: install-zig.sh DEST [CACHE_DIR]}
cache=${2:-}
arch=$(uname -m)

line=$(grep -E "^[0-9a-f]{64}  zig-$arch-linux-[0-9.]+\.tar\.xz\$" "$here/zig.sha256" || true)
if [ -z "$line" ]; then
    echo "install-zig.sh: no Zig release pinned for $arch in $here/zig.sha256" >&2
    exit 1
fi
tarball=${line##*  }
version=$(sed -E 's/^zig-[^-]+-linux-([0-9.]+)\.tar\.xz$/\1/' <<<"$tarball")
url="https://ziglang.org/download/$version/$tarball"

mkdir -p "$dest"
dest=$(cd "$dest" && pwd)
if [ -x "$dest/zig" ] && [ "$("$dest/zig" version 2>/dev/null)" = "$version" ]; then
    echo "$dest/zig"
    exit 0
fi

dir=${cache:-$dest}
mkdir -p "$dir"
if [ ! -f "$dir/$tarball" ]; then
    echo "install-zig.sh: downloading $url" >&2
    curl -fsSL --retry 3 -o "$dir/$tarball.part" "$url"
    mv "$dir/$tarball.part" "$dir/$tarball"
fi
# A cached tarball is checked again: the pin in zig.sha256 is the only thing trusted here.
if ! (cd "$dir" && printf '%s\n' "$line" | sha256sum -c --quiet -) >&2; then
    echo "install-zig.sh: $dir/$tarball does not match $here/zig.sha256; removing it" >&2
    rm -f "$dir/$tarball"
    exit 1
fi

rm -rf "$dest/lib" "$dest/zig" "$dest/doc"
tar -xJf "$dir/$tarball" -C "$dest" --strip-components=1
if [ "$("$dest/zig" version)" != "$version" ]; then
    echo "install-zig.sh: unpacked zig reports $("$dest/zig" version), wanted $version" >&2
    exit 1
fi
echo "install-zig.sh: Zig $version in $dest" >&2
echo "$dest/zig"
