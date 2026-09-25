#!/usr/bin/env bash
# Card #12JX — stage the situation this card fixes: Relay was killed outright while a subagent
# ran. The sandbox holds one workspace, one conversation, one thread file the worker never
# wrote a stop to (status `running`), and a saved window layout whose pane opens that session.
# Everything the app touches lives under ./sandbox — unstage.sh removes all of it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
export SB="$HERE/sandbox"
rm -rf "$SB"
mkdir -p "$SB/home" "$SB/project"
printf 'field notes\n' > "$SB/project/notes.txt"
XDG_DATA_HOME="$SB/home" RELAY_KEYRING=off RELAY_MEMORY_IMPORT=off \
  PYTHONPATH="$REPO/backend:$REPO/tests" python3 "$HERE/stage_files.py" "$SB"
echo
echo "staged under: $SB"
echo "then open:    XDG_DATA_HOME='$SB/home' RELAY_KEYRING=off RELAY_MEMORY_IMPORT=off '$REPO/build/relay'"
