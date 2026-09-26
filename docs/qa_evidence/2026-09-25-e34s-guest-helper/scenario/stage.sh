#!/usr/bin/env bash
# Stage the #E34S Try it: a disposable project whose Board holds three cards, and a Relay
# profile whose Main model is the guest Claude Code — the configuration the helper could not
# serve before this card (card #GH5T) and must serve now (card #E34S). No model and no network:
# nothing here starts a guest or answers anything.
#
#   ./stage.sh [STAGE_DIR] [RELAY_BIN]
#
# Prints STAGE=<dir> and GUEST=<preset>. The window then opens with:
#   RELAY_BIN=<relay> "$out/home/run.sh" [DISPLAY]     (see run.sh, written next to the profile)
set -euo pipefail
stage=${1:?stage dir, e.g. /tmp/rl-e34s}
guest=${GUEST:-guest:claude}
case $guest in guest:claude) dotdir=.claude ;; guest:codex) dotdir=.codex ;; *)
    echo "GUEST must be guest:claude or guest:codex" >&2; exit 2 ;; esac
rm -rf "$stage"; mkdir -p "$stage/project/.board/features" "$stage/home/.config/RelayTerminal"

# --- the project: a board with three cards to ask the helper about ---------------------------
cat > "$stage/project/.board/board.yaml" <<'EOF'
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: done, filter: 'status:done'}]
columns: [inbox, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
EOF
mkcard() { # id, file, title, question
  cat > "$stage/project/.board/features/$2" <<EOF
---
id: $1
type: work
status: inbox
labels: [feature]
rank: zzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'E34S Try it staging fixture'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# $3

## Issue
$4
EOF
}
mkcard W1A2 2026-09-25-water-the-office-plants.md "Water the office plants" \
    "How often should the office plants be watered? Answer in one sentence."
mkcard W3B4 2026-09-25-sharpen-the-pencils.md "Sharpen the pencils" \
    "Which of the pencils is the sharpest? Answer in one sentence."
mkcard W5C6 2026-09-25-file-the-receipts.md "File the receipts" \
    "Which receipts are still unfiled? Answer in one sentence."

# --- the profile: guest as Main, relay-free on the priority list for the card consoles -------
cat > "$stage/home/.config/RelayTerminal/relay.conf" <<EOF
[instructions]
onboarded=true
[security]
approvals_chosen=true
[provider]
preset=$guest
base=harness://claude
[models]
fallbacks=relay-free
EOF

# --- the guest's own auth stays with the user; the sandbox home points at it -----------------
for d in .claude .codex; do
    [ -e "$HOME/$d" ] && ln -sfn "$HOME/$d" "$stage/home/$d"
done

# --- the launcher: a real Relay under Xvfb on this profile -----------------------------------
cat > "$stage/home/run.sh" <<'EOF'
#!/usr/bin/env bash
#   RELAY_BIN=<relay> run.sh   — opens the staged window and prints the display and pid.
set -euo pipefail
bin=${RELAY_BIN:?set RELAY_BIN to a relay binary}
stage=$(cd "$(dirname "$0")/.." && pwd)
width=1600 height=1000
display=${1:-}; if [ -z "$display" ]; then
    for n in $(seq 170 199); do [ -e "/tmp/.X11-unix/X$n" ] || { display=:$n; break; }; done
fi
sandbox=$(mktemp -d /tmp/rl-e34s-home.XXXX)
mv "$stage/home/.config" "$sandbox/config-keep" 2>/dev/null || true
# The window outlives this script: kill RELAY_PID (and the Xvfb) when done with it.
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!
for i in $(seq 1 40); do
    [ -e "/tmp/.X11-unix/X${display#:}" ] && break
    sleep 0.5
done
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET          # discover only this disposable instance, never the caller's app
mkdir -p "$sandbox/run" "$sandbox/home/.config/RelayTerminal" "$sandbox/tmp"; chmod 700 "$sandbox/run"
cp "$sandbox/config-keep/RelayTerminal/relay.conf" "$sandbox/home/.config/RelayTerminal/relay.conf"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
for d in .claude .codex; do
    [ -e "$stage/home/$d" ] && ln -sfn "$(readlink "$stage/home/$d")" "$sandbox/home/$d"
done
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache
(cd "$stage/project" && exec "$bin" --workspace "$stage/project" --clean-shell --fresh) \
    >"$sandbox/relay.log" 2>&1 & relay_pid=$!
for i in $(seq 1 30); do
    [ -s "$sandbox/run/relay/open-socket" ] && break
    sleep 1
done
echo "DISPLAY=$display RELAY_PID=$relay_pid XVFB_PID=$xvfb_pid XDG_RUNTIME_DIR=$sandbox/run STAGE=$stage"
EOF
chmod +x "$stage/home/run.sh"
echo "STAGE=$stage GUEST=$guest PROJECT=$stage/project"
