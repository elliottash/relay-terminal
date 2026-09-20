#!/usr/bin/env bash
# The Profile button on the Switchboard's tool row (#7BM4 phase 5), driven in a real Relay under
# Xvfb: open the Switchboard on a fixture project that holds a board *and* a tiny Ninja-buildable
# CMake project, press Profile, shoot the target menu, choose "Build (this machine)", and shoot the
# result pane once the table is in it.
#
# Isolated HOME/XDG/TMPDIR under a short path (the 108-byte socket limit), RELAY_KEYRING=off so the
# run never touches the owner's real identity key, and the only input is xdotool chords and clicks —
# nothing is typed into a terminal pane.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
# The binary land.py built from the exact tree it committed; this shared checkout's build/relay
# carries other sessions' in-flight edits.
bin=${RELAY_BIN:-/tmp/claude-1000/land/profile/verify/build/relay}
out=${1:-$root/docs/qa_evidence/2026-09-20-profile-button}
width=1600 height=1000
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-pf.XXXX)
xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $xvfb_pid; do kill -TERM "$pid" 2>/dev/null; done;
            sleep 1; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.local/share" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

# The fixture: a board, and a three-target CMake project the build profile has something to time.
work=$HOME/project
mkdir -p "$work/src"
cat > "$work/CMakeLists.txt" <<'CM'
cmake_minimum_required(VERSION 3.16)
project(fixture CXX)
add_library(alpha src/alpha.cpp)
add_library(beta src/beta.cpp)
add_executable(fixture src/main.cpp)
target_link_libraries(fixture alpha beta)
CM
cat > "$work/src/alpha.cpp" <<'AL'
#include <map>
#include <string>
#include <vector>
int alpha() { std::map<std::string, std::vector<int>> m; m["a"].push_back(1); return (int)m.size(); }
AL
cat > "$work/src/beta.cpp" <<'BE'
#include <regex>
#include <sstream>
int beta() { std::regex r("a+b"); std::ostringstream o; o << std::regex_match("aab", r); return (int)o.str().size(); }
BE
printf 'int alpha(); int beta();\nint main() { return alpha() + beta() - 2; }\n' > "$work/src/main.cpp"
PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B
work = Path(sys.argv[1]); root = work / "issues"; root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
    "columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\n", encoding="utf-8")
b = B.Board(root, work)
c = B.new_card("work", "Make the build faster", "inbox", created="2026-09-20", rank="c",
               request="the build takes minutes; find out where the time goes", labels=["feature"])
B.write_new_card(b, c, "features")
FIX

(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 10
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"; xdotool windowfocus "$win"; sleep 6
xdotool key --window "$win" ctrl+shift+s; sleep 10                  # the Switchboard
# The first-run Approvals pane sits between the terminal and the board; "Allow everything" is
# where 00-board.png shows it, and answering it is what takes the pane out of the layout.
xdotool mousemove 752 556 click 1; sleep 4
import -window root "$out/00-board.png"

# The tool row is at the bottom of the list page, above the page agent's composer: Check, Clean up,
# Tests, Profile. PROFILE_X/PROFILE_Y are read off 00-board.png.
px=${PROFILE_X:-0} py=${PROFILE_Y:-0}
if (( px == 0 )); then echo "read Profile's coordinates off $out/00-board.png, then rerun with PROFILE_X/PROFILE_Y"; exit 0; fi
xdotool mousemove "$px" "$py" click 1; sleep 3
import -window root "$out/implementer-01-menu.png"
if [[ -n ${MENU_ONLY:-} ]]; then echo "menu shot only"; exit 0; fi

# "Build (this machine)" is the first entry, just under the button.
xdotool mousemove ${BUILD_X:-$((px + 60))} ${BUILD_Y:-$((py + 30))} click 1; sleep 4
import -window root "$out/02-started.png"
for _ in $(seq 1 24); do sleep 5; done            # the fixture build is seconds; give cmake room
import -window root "$out/implementer-02-result.png"
# "Attach to card…": the picker opens on the board's one card, Enter takes it, and the card's
# `## Profile` section and `links.evidence` are what this leaves behind.
if [[ -n ${ATTACH:-1} ]]; then
    xdotool mousemove ${ATTACH_X:-1299} ${ATTACH_Y:-970} click 1; sleep 4
    import -window root "$out/03-card-picker.png"
    xdotool key Return; sleep 5
    import -window root "$out/implementer-03-attached.png"
    cat "$work"/issues/features/*.md > "$out/card-after-attach.md" 2>/dev/null
fi
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
find "$work/docs/qa_evidence" -maxdepth 2 -type f 2>/dev/null | sed "s|$work/||" > "$out/evidence-listing.txt"
cp "$work"/docs/qa_evidence/*-profile-build/summary.md "$out/fixture-summary.md" 2>/dev/null
echo "shots in $out"
