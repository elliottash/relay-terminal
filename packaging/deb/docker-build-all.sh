#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Local equivalent of the release workflow: build each .deb in a container with the
# repository mounted read-only, then install and smoke-test it in a fresh container.
#
#   packaging/deb/docker-build-all.sh [OUT_DIR] [VERSION_SUFFIX] [IMAGE...]
#
# OUT_DIR/cache is mounted into every build as RELAY_CACHE_DIR: the pinned Zig tarball and
# the libghostty-vt archive built per distribution stay there, so a second run of the same
# pins skips the download and the Zig build (build-deb.sh, release.yml).
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=$(realpath -m "${1:-$repo/build-packages}")
suffix=${2:-}
shift $(( $# > 2 ? 2 : $# )) || true
images=("$@")
(( ${#images[@]} )) || images=(ubuntu:24.04 debian:trixie ubuntu:26.04)
mkdir -p "$out/logs" "$out/cache"
declare -A result
for image in "${images[@]}"; do
  tag=${image//[:.]/-}
  mkdir -p "$out/$tag"
  echo "=== build $image"
  if docker run --rm -v "$repo:/src:ro" -v "$out/$tag:/out" -v "$out/cache:/cache" \
      -e RELAY_CACHE_DIR=/cache "$image" \
      bash /src/packaging/deb/build-deb.sh /src /out "$suffix" >"$out/logs/build-$tag.log" 2>&1; then
    deb=$(ls "$out/$tag"/*.deb | head -n 1)
    echo "=== smoke $image ($(basename "$deb"))"
    if docker run --rm -v "$repo:/src:ro" -v "$out/$tag:/debs:ro" "$image" \
        bash /src/packaging/deb/smoke-test.sh "/debs/$(basename "$deb")" >"$out/logs/smoke-$tag.log" 2>&1; then
      result[$image]="PASS"
    else
      result[$image]="smoke FAIL (see $out/logs/smoke-$tag.log)"
    fi
  else
    result[$image]="build FAIL (see $out/logs/build-$tag.log)"
  fi
  echo "    ${result[$image]}"
done
echo
for image in "${images[@]}"; do printf '%-16s %s\n' "$image" "${result[$image]}"; done
[[ ! " ${result[*]} " =~ FAIL ]]
