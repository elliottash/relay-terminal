#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
for command in cmake c++ python3; do
  if ! command -v "$command" >/dev/null; then
    printf 'Missing %s. See README.md for Linux build dependencies.\n' "$command" >&2
    exit 1
  fi
done
# Configure and build through scripts/relay-build: it holds the build lock while
# several sessions share this checkout, and it stamps the objects it produced
# back to the start of the build, so a header edited during the compile is
# recompiled next time instead of being missed (CLAUDE.md, "Build through
# scripts/relay-build"). Arguments here are configure arguments, as before.
cmake_args=(--cmake-arg="-DCMAKE_INSTALL_PREFIX=${RELAY_PREFIX:-$HOME/.local}")
for arg in "$@"; do cmake_args+=(--cmake-arg="$arg"); done
RELAY_JOBS="${RELAY_JOBS:-2}" scripts/relay-build "${cmake_args[@]}"
ctest --test-dir build --output-on-failure
printf '\nBuilt Relay. Run: ./build/relay --workspace /path/to/project\n'
