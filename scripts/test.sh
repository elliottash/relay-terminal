#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# Sessions auto-save under XDG_DATA_HOME; keep test runs out of the real home directory.
XDG_DATA_HOME="$(mktemp -d)"
export XDG_DATA_HOME
trap 'rm -rf "$XDG_DATA_HOME"' EXIT
PYTHONPATH="$PWD/backend${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s tests -v
