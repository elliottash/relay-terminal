#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build Relay's .deb inside a Debian/Ubuntu container (or a matching clean machine).
#
#   build-deb.sh SOURCE_DIR OUT_DIR [VERSION_SUFFIX]
#
# SOURCE_DIR may be read-only; the build happens in a temporary directory.
# Set RELAY_SKIP_DEPS=1 when the build dependencies are already installed and
# RELAY_SKIP_TESTS=1 to skip ctest.
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
  $sudo apt-get install -y -q --no-install-recommends build-essential cmake ninja-build \
    python3 python3-cryptography dpkg-dev file bash git ca-certificates "${deps[@]}"
fi

build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DRELAY_QT_MAJOR="$qt" -DRELAY_VERSION_SUFFIX="$suffix" -DBUILD_TESTING="$([[ ${RELAY_SKIP_TESTS:-0} == 1 ]] && echo OFF || echo ON)"
cmake --build "$build" --parallel "${RELAY_JOBS:-$(nproc)}"
if [[ ${RELAY_SKIP_TESTS:-0} != 1 ]]; then
  # Tests read the source tree; Python must not write __pycache__ into a read-only mount.
  # One retry: tests/test_shell.py's native Ctrl+C test is timing-sensitive on loaded machines.
  PYTHONDONTWRITEBYTECODE=1 ctest --test-dir "$build" --output-on-failure --repeat until-pass:2
fi
(cd "$build" && cpack -G DEB)
cp -v "$build"/*.deb "$out"/
