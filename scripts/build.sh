#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
for command in cmake c++ python3; do
  if ! command -v "$command" >/dev/null; then
    printf 'Missing %s. See README.md for Linux build dependencies.\n' "$command" >&2
    exit 1
  fi
done
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="${RELAY_PREFIX:-$HOME/.local}" "$@"
cmake --build build --parallel "${RELAY_JOBS:-2}"
ctest --test-dir build --output-on-failure
printf '\nBuilt Relay. Run: ./build/relay --workspace /path/to/project\n'
