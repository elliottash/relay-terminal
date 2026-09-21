#!/usr/bin/env bash
# An option written from a console is announced and reversible — card #AGNT, QA item
# "An agent's option write is not announced".
#
#   drive.sh [relay-binary] [out-dir]
#
# Both paths are absolute; the script runs from its own directory. Isolation is the sibling
# drive's: Xvfb, a private HOME / XDG_* / TMPDIR under a short path (the 108-byte socket limit),
# RELAY_KEYRING=off, and no provider account — the profile points a local model endpoint at
# stub-provider.py on loopback, so the only agent in the run is that script.
# Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# What it drives, in the Options helper's console (a shell-less `Pane` on the tab's worker, which
# is the pipe an `app_command` out of any console really takes):
#
#   b01  the console answers and `app_option_set` reaches the setting (relay.conf)
#   b02  the **row** wears "changed by the agent just now: off → on" (§30.6)
#   b03  the bell's list carries "Agent changed Copy on select · off → on · Undo", and it is
#        inside the list's own viewport — the rectangles are read from RELAY_QA_RECTS and
#        compared, because the bug this drive is the gate for was an entry that was posted,
#        drawn, and clipped away below the fold
#   b04  Undo, clicked by rectangle, puts the setting back and takes the row's marker with it
#   b05  the *agent's* own `app_undo` is announced too, with a way back from it, and leaves the
#        mark on the row (it is still the agent holding it)
#   b06  `app_action_run` says "Agent ran Reload themes"
#
# Each check writes one PASS/FAIL line to notes.txt naming the shot it was read from.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
width=1500 height=1100
port=${RELAY_QA_PORT:-8893}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-undo.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do [[ -n $pid ]] && kill "$pid" 2>/dev/null; done
    sleep 0.5
    for pid in $relay_pid $stub_pid $xvfb_pid; do [[ -n $pid ]] && kill -9 "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT INT TERM

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
# Where the named widgets are: the bell is an icon, and the notification list's viewport and the
# Undo button on an entry are things OCR can read the words of but not the bounds of.
export RELAY_QA_RECTS=$sandbox/rects.json
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

python3 "$PWD/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
sleep 1
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[approvals]
mode=allow
[agent]
app_writes=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }

win=
t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }
# No compositor under Xvfb: a damaged region is sometimes left unpainted, so the window is
# resized by a pixel and back to force a full expose before every capture.
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.5
    xdotool windowsize "$win" $((width - 1)) "$height"; sleep 0.2
    xdotool windowsize "$win" "$width" "$height"; sleep 0.6
    import -window "$win" "$out/$1.png"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
# A `Qt::Popup` is its own top-level window, so `import -window $win` leaves a hole where it is.
shotroot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.5; import -window root "$out/$1.png"; sleep 0.3; }
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) printf "%d %d\n", x, y}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
click_word() { local at; at=$(word_xy "$1" "$2"); [[ -n $at ]] && click_at ${at% *} ${at#* }; }
# The same, but only in the top band of the window — the Options pane's tab row. The word
# "Terminal" is also prose in a transcript, and word_xy takes the last match it finds.
click_word_top() {
    local at; at=$(words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) && $3 < 250 {printf "%d %d\n", $2+$4/2, $3+$5/2; exit}')
    [[ -n $at ]] && click_at ${at% *} ${at#* }
}
has()   { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
awaited() {
    local waited=0 limit=${4:-180}
    while :; do
        shot "$2"
        text "$2" | grep -qi -- "$3" && { ok "$1 (\"$3\" in $2.png)"; return 0; }
        (( waited >= limit )) && { bad "$1: no \"$3\" in $2.png after ${limit}s"; return 1; }
        sleep 3; waited=$((waited + 3))
    done
}
# The widget by name, out of RELAY_QA_RECTS: screen coordinates, and the window sits at 0,0.
click_rect() {
    local xy; xy=$(python3 - "$RELAY_QA_RECTS" "$1" <<'PY' 2>/dev/null
import json, sys
rows = json.load(open(sys.argv[1]))
row = rows.get(sys.argv[2])
if not row: sys.exit(1)
print(row["x"] + row["w"] // 2, row["y"] + row["h"] // 2)
PY
) || { note "  (no widget named $1 on screen)"; return 1; }
    xdotool mousemove ${xy% *} ${xy#* } click 1
    sleep 1.5
}
# A button of the notification list, by the words on it — object names are not unique ("Clear
# all" and every entry's Undo are all `popupTextButton`), and the text is what tells them apart.
# Prints "x y" of its centre, and nothing when no such button is on screen.
button_xy() {
    python3 - "$RELAY_QA_RECTS" "$1" <<'PY' 2>/dev/null
import json, sys
rows = json.load(open(sys.argv[1]))
want = sys.argv[2].lower()
for key, row in rows.items():
    if key.split("#")[0] == "popupTextButton" and row.get("text", "").strip().lower() == want:
        print(row["x"] + row["w"] // 2, row["y"] + row["h"] // 2)
        break
PY
}
# Is the widget whose text is $1 wholly inside the list's own viewport? This is the check the
# bug needed: the entry existed, was drawn, and was clipped away under the bottom edge.
inside_list() {
    python3 - "$RELAY_QA_RECTS" "$1" <<'PY' 2>/dev/null
import json, sys
rows = json.load(open(sys.argv[1]))
want = sys.argv[2].lower()
view = rows.get("notificationsScroll")
if not view:
    print("no viewport"); raise SystemExit(1)
top, bottom = view["y"], view["y"] + view["h"]
for key, row in rows.items():
    if row.get("text", "").strip().lower() != want:
        continue
    lo, hi = row["y"], row["y"] + row["h"]
    print(f'{key} y={lo}..{hi} viewport y={top}..{bottom}')
    raise SystemExit(0 if lo >= top and hi <= bottom else 2)
print(f'nothing on screen reads "{want}"')
raise SystemExit(1)
PY
}
conf() { grep -i copy_on_select "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null || echo '(absent)'; }

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 12
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
shot b00-start
at=$(word_xy b00-start "recommend"); [[ -n $at ]] && click_at ${at% *} ${at#* }
sleep 2

note "===== an option written from the Options helper's console ====="
k ctrl+shift+o; sleep 5
k alt+q; sleep 4
shot b01a-console
has "b1 Alt+Q expands the Options helper into a console" b01a-console "Ask the Options helper"
click_word b01a-console "Ask"; sleep 1
t "turn on copy on select for me"; k Return
awaited "b1 the console says what it changed" b01-changed "turned Copy on select on" 240
note "relay.conf: $(conf)"
if grep -qi "copy_on_select=true" "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; then
    ok "b1 the write from a console reached the setting"
else
    bad "b1 the write from a console did not reach the setting"
fi

# The row, on the Terminal page — which is not the page Options opens on.
click_word_top b01-changed "Terminal"; sleep 2; shot b02-row-marked
has "b2 the row says an agent changed it" b02-row-marked "changed by the agent"
has "b2 and says from what to what" b02-row-marked "off"

# The notice, in the bell's list. `inside_list` is the gate: before this was fixed the entry was
# posted and drawn below the viewport's bottom edge, which is what "no notification appeared"
# turned out to mean.
click_rect windowBellButton; sleep 2; shotroot b03-notice
has "b3 the list carries the agent's change" b03-notice "Agent changed Copy on select"
has "b3 and says from what to what" b03-notice "off"
if seen=$(inside_list Undo); then
    ok "b3 the Undo button is inside the list's viewport ($seen)"
else
    bad "b3 the Undo button is not inside the list's viewport ($seen)"
fi

# Undo, by rectangle: the words "Undo" also appear in the *body* of the turn's own "Agent
# finished" entry, which is how an earlier drive clicked nothing and read it as a failure.
at=$(button_xy Undo)
if [[ -n $at ]]; then
    click_at ${at% *} ${at#* }
else
    bad "b4 no Undo button on screen to click"
fi
k Escape          # the popup is its own window and would be a hole in the capture below
sleep 2; shot b04-undone
note "relay.conf after Undo: $(conf)"
if grep -qi "copy_on_select=true" "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; then
    bad "b4 Undo did not put Copy on select back (still true in relay.conf)"
else
    ok "b4 Undo put Copy on select back"
fi
hasnt "b4 and the row's marker went with the change" b04-undone "changed by the agent"

# The agent taking its own change back: a second write, then app_undo out of the console.
click_word b04-undone "Ask"; sleep 1
t "turn on copy on select for me"; k Return
awaited "b5 a second write lands" b05a-again "turned Copy on select on" 240
click_word b05a-again "Ask"; sleep 1
t "put copy on select back please"; k Return
awaited "b5 the agent says it put the setting back" b05b-reverted "put Copy on select back" 240
note "relay.conf after the agent's own undo: $(conf)"
if grep -qi "copy_on_select=true" "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; then
    bad "b5 the agent's app_undo did not put the setting back"
else
    ok "b5 the agent's app_undo put the setting back"
fi
click_rect windowBellButton; sleep 2; shotroot b05-agent-undo
has "b5 the agent's own revert is announced" b05-agent-undo "Agent changed Copy on select"
has "b5 and the offer it answered says it was taken" b05-agent-undo "Undone: Copy on select"
if seen=$(inside_list Undo); then
    ok "b5 with a way back from the revert, in the viewport ($seen)"
else
    bad "b5 the revert offers no reachable way back ($seen)"
fi
k Escape; sleep 1
shot b05c-row
has "b5 the row is still the agent's until the person touches it" b05c-row "changed by the agent"

# An action, on the same pipe.
click_word b05c-row "Ask"; sleep 1
t "reload the themes for me"; k Return
awaited "b6 the console ran the action" b06a-action "reloaded the themes" 240
click_rect windowBellButton; sleep 2; shotroot b06-action
has "b6 the list says which action it ran" b06-action "Agent ran Reload themes"
k Escape

note "----"
note "PASS $pass  FAIL $fail"
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
cp "$RELAY_QA_RECTS" "$out/rects-last.json" 2>/dev/null
cat "$out/notes.txt"
