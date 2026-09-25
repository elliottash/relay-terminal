#!/usr/bin/env bash
# Card #12JX — mechanical proof of the staged sandbox (no GUI, no model): resume the staged
# conversation the way the restored pane does and check the thread comes back interrupted.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
XDG_DATA_HOME="$HERE/sandbox/home" RELAY_KEYRING=off RELAY_MEMORY_IMPORT=off \
  PYTHONPATH="$REPO/backend:$REPO/tests" python3 "$HERE/check.py" "$HERE/sandbox"
