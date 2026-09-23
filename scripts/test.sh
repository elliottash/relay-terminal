#!/usr/bin/env bash
set -euo pipefail
# `scripts/test.sh` runs the Python and bash suite exactly as it always has.
# `scripts/test.sh --junit <path>` runs the same suite through relay_core.junit_runner instead,
# which writes one JUnit <testcase> per test (duration, file, line, outcome) for the tests store
# (#7BM4).  Everything after the path is passed on as dotted test names, so
# `scripts/test.sh --junit /tmp/j.xml tests.test_board` runs one module.
junit=""
if [[ "${1:-}" == "--junit" ]]; then
    [[ $# -ge 2 ]] || { echo "usage: $0 --junit <path> [test names…]" >&2; exit 2; }
    junit="$2"
    # Resolve before the cd below, so a relative path means the caller's directory.
    [[ "$junit" = /* ]] || junit="$PWD/$junit"
    shift 2
fi
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# Sessions auto-save under XDG_DATA_HOME; keep test runs out of the real home directory.
XDG_DATA_HOME="$(mktemp -d)"
export XDG_DATA_HOME
export RELAY_LOG_ORIGIN=test
export RELAY_LOG_RUN_ID="test-$(date -u +%Y%m%dT%H%M%SZ)-$$"
# Never reach the user's desktop keyring from tests (workers spawned by tests inherit this).
export RELAY_KEYRING=off
# Nor the user's saved local model servers: a loopback URL in a test must not match a real endpoint.
export RELAY_LOCAL_MODELS="$XDG_DATA_HOME/local-models.json"
# Nor import the user's Claude Code / Codex memories into their real global Board (#MEMS).
export RELAY_MEMORY_IMPORT=off
trap 'rm -rf "$XDG_DATA_HOME"' EXIT
if [[ -n "$junit" ]]; then
    PYTHONPATH="$PWD/backend${PYTHONPATH:+:$PYTHONPATH}" \
        python3 -m relay_core.junit_runner --junit "$junit" -s tests -v "$@"
else
    PYTHONPATH="$PWD/backend${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s tests -v
fi
