#!/bin/sh
# The queue gate's tests (card #3MH4), run from the mirrored candidate source with its build dir.
# main was already red when the gate began, so both suites judge against a known-failure list:
# ctest skips the cases in .relay/known-ctest-failures.txt, and backend-and-bash — the same
# Python suite as scripts/test.sh, which timed out at 600 s under -j 8 — is skipped there and
# run once by scripts/gate-known-failures.py, which fails only on failures not in
# .relay/known-failures.txt. `--no-tests=error` still fails an empty ctest run.
set -eu
build="$1"
known=$(grep -v '^#' .relay/known-ctest-failures.txt | grep -v '^$' | paste -sd'|' -)
ctest --test-dir "$build" --output-on-failure --no-tests=error -j "${RELAY_JOBS:-2}" \
    -E "^(backend-and-bash${known:+|$known})\$"
exec python3 scripts/gate-known-failures.py
