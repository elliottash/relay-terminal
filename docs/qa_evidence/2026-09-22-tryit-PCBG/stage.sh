#!/usr/bin/env bash
# Stage #PCBG for a person: open Relay with one terminal pane that has a live background
# job (`sleep 600 &`), so closing the pane shows the "Work is still running" dialog.
# Rerunnable: it rebuilds the fixture each time. Isolated profile — nothing of yours is touched.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
FIX="${PCBG_FIXTURE:-/tmp/claude-1000/tryit/pcbg}"
rm -rf "$FIX"
mkdir -p "$FIX/home/.config/RelayTerminal" "$FIX/run" "$FIX/tmp" "$FIX/work"
chmod 700 "$FIX/run"
cat > "$FIX/home/.config/RelayTerminal/relay.conf" <<'EOF'
[instructions]
onboarded=true
[security]
approvals_chosen=true
[windows]
restore=false
EOF
# Relay starts every pane as: bash --noprofile --rcfile <data>/shell/integration.bash -i.
# Seed the job through a private copy of that rcfile (Relay ignores $SHELL by design).
# RELAY_DATA_DIR resolves only when <dir>/backend/worker.py exists, so link the real backend.
ln -s "$ROOT/backend" "$FIX/backend"
cp -r "$ROOT/shell" "$FIX/shell"
cat >> "$FIX/shell/integration.bash" <<'EOF'

# Staged active work for the #PCBG try-it: a background job this pane's shell owns.
sleep 600 &
echo "This pane has a background job running: sleep 600 (pid $!)."
EOF
env -u RELAY_OPEN_SOCKET -u RELAY_DATA_DIR \
    HOME="$FIX/home" XDG_CONFIG_HOME="$FIX/home/.config" \
    XDG_DATA_HOME="$FIX/home/.local/share" XDG_CACHE_HOME="$FIX/home/.cache" \
    XDG_RUNTIME_DIR="$FIX/run" TMPDIR="$FIX/tmp" RELAY_DATA_DIR="$FIX" \
    RELAY_KEYRING=off \
    "$ROOT/build/relay" --workspace "$FIX/work" --clean-shell --fresh \
    > "$FIX/relay.log" 2>&1 &
echo "Relay is open with a staged background job in its terminal pane (fixture: $FIX; log: $FIX/relay.log)."
