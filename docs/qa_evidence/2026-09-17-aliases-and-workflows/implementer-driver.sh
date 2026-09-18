#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Implementer evidence driver for aliases and workflows (issue G8DK). Runs Relay under Xvfb with an
# isolated HOME/XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_STATE_HOME and exercises:
#   1. defining an alias from the prompt box,
#   2. running one with parameters from each of the three paths (palette, /name, the name typed in
#      terminal mode),
#   3. the import preview for Warp workflows and shell aliases.
#
# The jail gets a COPY of this machine's real ~/.local/state/warp-terminal/warp.sqlite when there is
# one, so the import preview is exercised against real Warp workflows; the real file is never
# opened by the run. The jail's .bashrc carries a couple of shell aliases, including a deliberately
# malformed line. No key is read, printed or captured: aliases need no model (RELAY_KEYRING=off).
#
# Usage: docs/qa_evidence/2026-09-17-aliases-and-workflows/implementer-driver.sh [repo root] [build]
set -euo pipefail

ROOT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${2:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)

export HOME="$JAIL/home"
export XDG_CONFIG_HOME="$JAIL/config"
export XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache"
export XDG_STATE_HOME="$JAIL/state"
export RELAY_KEYRING=off
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$XDG_STATE_HOME/warp-terminal" "$JAIL/work"
export DISPLAY=:96

# A project with a Switchboard, so local aliases land in issues/aliases/.
cd "$JAIL/work"
git init -q . 2>/dev/null || true
mkdir -p issues
printf 'version: 1\n' > issues/board.yaml

# Shell aliases for the importer to find, including one line it must refuse to guess at.
cat >"$HOME/.bashrc" <<'RC'
alias gs='git status --short --branch'
alias ll='ls -alF'
alias hello='echo hello, world'
alias broken='never closed
RC

# Real Warp workflows, copied so the live database is untouched.
REAL_WARP="/home/$(id -un)/.local/state/warp-terminal/warp.sqlite"
if [ -f "$REAL_WARP" ]; then
    cp "$REAL_WARP" "$XDG_STATE_HOME/warp-terminal/warp.sqlite"
    echo "copied real Warp workflows from $REAL_WARP"
else
    echo "no Warp database on this machine; the preview will show shell aliases only"
fi

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=glm-coding

[instructions]
onboarded=true

[hints]
enabled=true
CONF

Xvfb "$DISPLAY" -screen 0 1500x950x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" 2>/dev/null || true; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2

"$BUILD/relay" >"$JAIL/relay-stdout.txt" 2>"$JAIL/relay-stderr.txt" &
APP=$!
sleep 8

shot() { import -window root "$OUT/implementer-$1.png"; sleep 1; }
type_() { xdotool type --delay 30 "$1"; sleep 1; }
clear_box() { xdotool key ctrl+a; xdotool key BackSpace; sleep 1; }
palette() { xdotool key ctrl+shift+a; sleep 2; type_ "$1"; sleep 1; }

shot 01-startup

# ---- 1. define an alias from the prompt box -------------------------------------------------
# Terminal mode, so it is saved as a command.
xdotool key ctrl+i; sleep 1
clear_box
type_ 'git log --oneline -{{count}} -- {{path}}'
shot 02-command-to-save
palette "save the prompt box"
shot 03-palette-save-as-alias
xdotool key Return; sleep 2
shot 04-name-the-alias
xdotool key ctrl+a; xdotool key BackSpace
type_ "recent"
shot 05-alias-named
xdotool key Return; sleep 3
shot 06-alias-saved

# What landed on disk: a Switchboard card in the repository Switchboard.
{ echo "=== issues/aliases ==="; ls -la "$JAIL/work/issues/aliases/" 2>&1
  echo; echo "=== recent.md ==="; cat "$JAIL/work/issues/aliases/recent.md" 2>&1; } \
  > "$OUT/implementer-alias-card.txt"

# ---- 2a. run from the palette ---------------------------------------------------------------
clear_box
palette "aliases"
shot 07-palette-aliases-submenu
xdotool key Return; sleep 2
type_ "recent"
shot 08-alias-list
# The command with two parameters: run it and fill the fields.
xdotool key Return; sleep 3
shot 09-fields-in-the-composer
type_ "5"; sleep 1
shot 10-first-field-typed
xdotool key Tab; sleep 1
shot 11-tab-to-the-next-field
type_ "src"; sleep 1
xdotool key Return; sleep 4
shot 12-ran-from-the-palette

# ---- 2b. run from /name ----------------------------------------------------------------------
clear_box
type_ "/rec"
shot 13-slash-popup-knows-the-alias
xdotool key ctrl+a; xdotool key BackSpace
type_ "/recent 3 docs"
shot 14-slash-with-arguments
xdotool key Return; sleep 4
shot 15-ran-from-slash-name

# ---- 2c. type the name in terminal mode ------------------------------------------------------
clear_box
type_ "recent 2 backend"
shot 16-name-typed-in-terminal-mode
xdotool key Return; sleep 4
shot 17-ran-from-the-typed-name

# A parameter value that is shell metacharacters: it must stay one word.
clear_box
type_ '/recent 1 a;touch /tmp/relay-alias-should-not-exist'
xdotool key Return; sleep 4
shot 18-metacharacters-stay-one-word
ls /tmp/relay-alias-should-not-exist >"$OUT/implementer-injection-check.txt" 2>&1 \
    && echo "FAILED: the injected command ran" >>"$OUT/implementer-injection-check.txt" \
    || echo "OK: the injected command never ran" >>"$OUT/implementer-injection-check.txt"

# ---- 2d. a prompt alias, from the global Switchboard, with its default ------------------------
# A second alias, a prompt this time, written straight to the store so the run paths have both.
mkdir -p "$XDG_CONFIG_HOME/relay/switchboard/aliases"
cat >"$XDG_CONFIG_HOME/relay/switchboard/aliases/explain.md" <<'MD'
---
id: X4TP
type: alias
status: active
name: explain
kind: prompt
rank: 0i
created: '2026-09-17'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Explain a file

## Run

Explain what {{path}} does, in three sentences.

## Parameters

- `path` = `README.md` — the file to explain
MD

# `explain` was written to the global Switchboard after the pane started, so this also shows the
# list being re-read: it is only in the palette because opening the palette asks for it again.
clear_box
type_ "/expl"
shot 19-slash-finds-the-global-prompt-alias
xdotool key ctrl+a; xdotool key BackSpace
type_ "/explain"
xdotool key Return; sleep 3
shot 20-prompt-alias-expanded-with-its-default
# With no provider configured a prompt submission opens the model dialog: close it.
xdotool key Escape; sleep 2

# ---- 3. the import preview --------------------------------------------------------------------
clear_box
palette "import warp workflows"
shot 21-palette-import-action
xdotool key Return; sleep 5
shot 22-import-preview
# Tick one row and import it.
xdotool key Down; sleep 1
shot 23-preview-row-selected
xdotool key Return; sleep 4
shot 24-imported

{ echo "=== global switchboard ==="; find "$XDG_CONFIG_HOME/relay/switchboard" -type f 2>&1
  echo; echo "=== local switchboard ==="; find "$JAIL/work/issues/aliases" -type f 2>&1; } \
  > "$OUT/implementer-after-import.txt"

cp "$XDG_DATA_HOME/relay/logs/relay.log" "$OUT/implementer-relay.log" 2>/dev/null || true
cp "$XDG_DATA_HOME/relay/logs/worker.log" "$OUT/implementer-worker.log" 2>/dev/null || true
cp "$JAIL/relay-stderr.txt" "$OUT/implementer-relay-stderr.log" 2>/dev/null || true
echo "screenshots in $OUT"
