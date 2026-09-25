#!/usr/bin/env bash
# #PCJY Try it — one command, no model, no network. Runs twice: once against the
# backend as it was before the fix (git 585d0844, the parent of 01affa57), once
# against this checkout. Prints, both times, what a restored guest pane asks the
# claude CLI to do after a Relay restart.
set -euo pipefail
EVIDENCE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$EVIDENCE/../../.." && pwd)"
RUN=/home/elliott/.cache/relay/scratch/tryit/pcjy
mkdir -p "$RUN"

export RELAY_KEYRING=off RELAY_MEMORY_IMPORT=off RELAY_LOG_ORIGIN=tryit
export XDG_DATA_HOME="$RUN/xdg"; mkdir -p "$XDG_DATA_HOME"

# The pristine backend (before the fix), materialised from git — never the working tree.
OLD="$RUN/backend-before"
mkdir -p "$OLD"
git -C "$REPO" archive "$(git -C "$REPO" rev-parse 01affa57^)" backend | tar -x -C "$OLD"

echo "== BEFORE the fix ($(git -C "$REPO" rev-parse --short 01affa57^)): a restored guest pane"
( cd "$EVIDENCE" && PYTHONPATH="$OLD/backend" python3 drive.py )

echo
echo "== AFTER the fix ($(git -C "$REPO" rev-parse --short 59635ce2)): same pane, same restart"
( cd "$EVIDENCE" && PYTHONPATH="$REPO/backend" python3 drive.py )
