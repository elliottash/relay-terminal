#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# Sessions auto-save under XDG_DATA_HOME; keep test runs out of the real home directory.
XDG_DATA_HOME="$(mktemp -d)"
export XDG_DATA_HOME
# Never reach the user's desktop keyring from tests (workers spawned by tests inherit this).
export RELAY_KEYRING=off
# Nor the user's saved local model servers: a loopback URL in a test must not match a real endpoint.
export RELAY_LOCAL_MODELS="$XDG_DATA_HOME/local-models.json"
trap 'rm -rf "$XDG_DATA_HOME"' EXIT
PYTHONPATH="$PWD/backend${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s tests -v
