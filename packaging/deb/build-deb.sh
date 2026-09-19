#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build Relay's .deb inside a Debian/Ubuntu container (or a matching clean machine).
#
#   build-deb.sh SOURCE_DIR OUT_DIR [VERSION_SUFFIX]
#
# SOURCE_DIR may be read-only; the build happens in a temporary directory.
# Set RELAY_SKIP_DEPS=1 when the build dependencies are already installed and
# RELAY_SKIP_TESTS=1 to skip ctest.
#
# The package ships the libghostty-vt emulator core (docs/ENGINE.md), which VtCoreFactory
# makes the default core when it is compiled in. It is built here, before Relay, from the
# ghostty commit pinned in engine/scripts/build-libghostty-vt.sh, with the Zig release
# pinned in engine/scripts/zig.sha256 (downloaded from ziglang.org and checked against that
# file) and -Dcpu=baseline, and linked statically: the .deb gains no runtime dependency.
# A failed download or Zig build fails the package build; nothing falls back to libvterm on
# its own. RELAY_WITH_GHOSTTY=0 builds a libvterm-only package on purpose.
# RELAY_CACHE_DIR keeps the Zig tarball and the built libghostty-vt prefix between runs
# (docker-build-all.sh mounts one, release.yml caches it); RELAY_BUILD_DIR keeps the build
# tree instead of a temporary directory, for inspecting what was built.
set -euo pipefail
src=$(realpath "$1")
out=$(realpath -m "$2")
suffix=${3:-}
mkdir -p "$out"

. /etc/os-release
case "$ID:$VERSION_ID" in
  ubuntu:24.04)
    deps=(qtbase5-dev libkf5parts-dev libkf5coreaddons-dev libkf5syntaxhighlighting-dev qtpdf5-dev)
    qt=5 ;;
  debian:13|ubuntu:26.04|ubuntu:25.10)
    # qt6-pdf-dev is left out until src/FilePanes.cpp uses Qt6's scoped QPdfView enums
    # (QPdfView::PageMode::MultiPage, QPdfView::ZoomMode::FitToWidth); PDF preview is off.
    deps=(qt6-base-dev libkf6parts-dev libkf6coreaddons-dev libkf6syntaxhighlighting-dev)
    qt=6 ;;
  *)
    echo "build-deb.sh: unsupported distribution $ID $VERSION_ID" >&2
    exit 2 ;;
esac

if [[ ${RELAY_SKIP_DEPS:-0} != 1 ]]; then
  sudo=; [[ $(id -u) == 0 ]] || sudo=sudo
  export DEBIAN_FRONTEND=noninteractive
  $sudo apt-get update -q
  # bash and git are for the tests (PTY tests need Bash; the router tests expect common
  # commands such as git on PATH); dpkg-dev and file are what CPack's dpkg-shlibdeps needs;
  # python3-cryptography is a runtime dependency (remote access, Relay Free) whose tests run here.
  # curl and xz-utils fetch and unpack the pinned Zig release for libghostty-vt.
  $sudo apt-get install -y -q --no-install-recommends build-essential cmake ninja-build \
    python3 python3-cryptography dpkg-dev file bash git ca-certificates curl xz-utils "${deps[@]}"
fi

if [[ -n ${RELAY_BUILD_DIR:-} ]]; then
  build=$(realpath -m "$RELAY_BUILD_DIR"); mkdir -p "$build"
else
  build=$(mktemp -d)
  trap 'rm -rf "$build"' EXIT
fi

ghostty_flags=()
if [[ ${RELAY_WITH_GHOSTTY:-1} != 0 ]]; then
  cache=${RELAY_CACHE_DIR:-$build/cache}
  echo "== libghostty-vt core (Zig from engine/scripts/zig.sha256, cache: $cache)"
  zig=$(bash "$src/engine/scripts/install-zig.sh" "$build/zig" "$cache/zig")
  prefix=$cache/ghostty-vt/$ID$VERSION_ID-$(uname -m)
  GHOSTTY_SRC_DIR=$build/ghostty-src RELAY_GHOSTTY_ZIG_CPU=baseline \
    bash "$src/engine/scripts/build-libghostty-vt.sh" "$prefix" "$zig" || {
      echo "build-deb.sh: libghostty-vt did not build; the package must ship the ghostty core" \
           "(RELAY_WITH_GHOSTTY=0 builds a libvterm-only package on purpose)" >&2
      exit 1
    }
  ghostty_flags=(-DRELAY_ENGINE_WITH_GHOSTTY=ON "-DRELAY_GHOSTTY_VT_PREFIX=$prefix")
else
  echo "== RELAY_WITH_GHOSTTY=0: building a libvterm-only package" >&2
fi

cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DRELAY_QT_MAJOR="$qt" -DRELAY_VERSION_SUFFIX="$suffix" -DBUILD_TESTING="$([[ ${RELAY_SKIP_TESTS:-0} == 1 ]] && echo OFF || echo ON)" \
  "${ghostty_flags[@]}"
cmake --build "$build" --parallel "${RELAY_JOBS:-$(nproc)}"
if [[ ${RELAY_WITH_GHOSTTY:-1} != 0 ]]; then
  # The core is in the binary or the build stops here: TerminalSession falls back to the first
  # available core silently, so only the binary can prove which one the package carries.
  grep -qF 'Relay(libghostty-vt)' "$build/relay" ||
    { echo "build-deb.sh: $build/relay was built without the libghostty-vt core" >&2; exit 1; }
  echo "== relay carries the libghostty-vt core (default core: ghostty)"
fi
if [[ ${RELAY_SKIP_TESTS:-0} != 1 ]]; then
  # Tests read the source tree; Python must not write __pycache__ into a read-only mount.
  # One retry: tests/test_shell.py's native Ctrl+C test is timing-sensitive on loaded machines.
  PYTHONDONTWRITEBYTECODE=1 ctest --test-dir "$build" --output-on-failure --repeat until-pass:2
fi
(cd "$build" && cpack -G DEB)
cp -v "$build"/*.deb "$out"/
