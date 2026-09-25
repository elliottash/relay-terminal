#!/usr/bin/env bash
# #G2C7 Try-it staging: a disposable project with seeded sessions and a seeded board, and
# Relay running on it under an isolated profile, so the Sessions pane's instant filter and
# the Board's ranked filter can be judged without touching the owner's data.
set -euo pipefail

SB=/home/elliott/.cache/relay/scratch/tryit/g2c7
rm -rf "$SB"
mkdir -p "$SB"/{home,data,run,shots,proj/.board/features,proj/.board/threads}
: > "$SB/relay.log"

# --- the disposable project and its board ------------------------------------------------
cd "$SB/proj"
git init -q . && git commit -q --allow-empty -m "fixture"

card() {  # id title status body
    cat > ".board/features/$3-$(echo "$2" | tr ' ' '-').md" <<EOF
---
id: $1
title: "$2"
status: $3
tab: features
labels: [feature, sessions]
created: 2026-09-25
updated: 2026-09-25
rank: m
source: "try-it fixture"
private: false
priority: 0
---

# $2

$4
EOF
}
card G2C7 "Sorting the list" needs-verification "exact-title first, then word starts, then contains — the four asks live in the sessions pane"
card MDSG "Sessions search costs 100 ms per key" done "the sqlite pass was made async; nothing visual"
card Q1W2 "unrelated bake-off" inbox "the sorting by hand is fine, honestly, but nobody wants to wait"
# G2C7 is claimed now (a live session token) AND was claimed once (its thread records it).
sed -i 's/^updated: 2026-09-25/updated: 2026-09-25\nsession: 0123456789abcdef/' ".board/features/needs-verification-Sorting-the-list.md"
cat > .board/threads/G2C7.md <<'EOF'
- kind: event
  when: 2026-09-25T10:00:00Z
  agent claimed this card
- kind: progress
  when: 2026-09-25T10:01:00Z
  Claimed (0123456789abcdef)
EOF
cat MDSG Q1W2 2>/dev/null || true
cat > .board/board.yaml <<'EOF'
name: try-it
statuses: [inbox, planned, executing, needs-verification, done]
default_tab: features
EOF

# --- seeded sessions under an isolated data root ------------------------------------------
PY=python3
$PY - <<'EOF'
import json, pathlib, time
root = pathlib.Path('/home/elliott/.cache/relay/scratch/tryit/g2c7')
sessions = root / 'data' / 'sessions' / 'e1'
sessions.mkdir(parents=True, exist_ok=True)
now = time.time()
def seed(sid, title, messages, updated):
    doc = {
        'id': sid, 'title': title, 'workspace': str(root / 'proj'),
        'model': 'claude-fable', 'models': ['claude-fable'],
        'created': now - 86400 * 3, 'updated': updated, 'turns': len(messages),
        'open_requests': 0, 'version': 3, 'mode': 'build', 'kind': 'session',
        'messages': [{'role': role, 'content': text, 'relay_kind': kind}
                     for role, text, kind in messages],
    }
    (sessions / f'{sid}.json').write_text(json.dumps(doc))
seed('a' * 32, 'sortable columns', [
    ('user', 'keep the columns i sort by — mention #G2C7 and #MDSG for me', 'prompt'),
    ('assistant', 'kept: Updated, Turns, Requests, Model, Tokens, Recap.', 'answer')], now - 600)
seed('b' * 32, 'sorting the sessions table', [
    ('user', 'the title should straddle the columns; #MDSG made the sqlite side fast', 'prompt')], now - 3600)
seed('c' * 32, 'sessions table recap', [
    ('user', 'a recap column was added', 'prompt')], now - 7200)
seed('d' * 32, 'unrelated bake-off', [
    ('user', 'the sorting must be instant, not a sqlite wait', 'prompt')], now - 10800)
seed('e' * 32, 'board parity notes', [
    ('user', 'the same behaviour on the board, for #G2C7', 'prompt')], now - 14400)
EOF

# --- Relay on the fixture, isolated --------------------------------------------------------
export HOME="$SB/home" RELAY_DATA_DIR="$SB/data" XDG_RUNTIME_DIR="$SB/run" XDG_CONFIG_HOME="$SB/home/.config"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"
Xvfb :99 -screen 0 1440x1000x24 >/dev/null 2>&1 &
echo $! > "$SB/xvfb.pid"
export DISPLAY=:99
sleep 1
cd "$SB/proj"
/home/elliott/repos/relay-terminal/build/relay . > "$SB/relay.log" 2>&1 &
echo $! > "$SB/relay.pid"
sleep 8
echo "open line: cd $SB/proj && RELAY_DATA_DIR=$SB/data XDG_RUNTIME_DIR=$SB/run HOME=$SB/home DISPLAY=:99 scripts/relay-drive panes"
