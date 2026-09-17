#!/usr/bin/env bash
# Clickable paths (#YZTK) and the keyboard walk over them (#GWXM), live on both engines
# under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-clickable-paths/drive.sh [build-dir] [engine]
#
# engine: "relay" (default, the process default) or "konsole" (--engine=konsole).
# Writes implementer-<engine>-NN-*.png next to this script plus <engine>-relay-stderr.log. An isolated
# XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real profile.
#
# Root-window captures come back black on this host, so every screenshot is
# `import -window "$win"` of the Relay window itself.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
engine=${2:-relay}
# A free display (other sessions on this host use their own).
display=
for n in $(seq 140 180); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free X display"; exit 1; }
echo "display $display"

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

# ---- the output the run clicks on -------------------------------------------------------
mkdir -p "$work/src" "$work/tests" "$work/notes"
cat >"$work/src/main.cpp" <<'EOF'
#include <cstdio>
int main() { std::printf("hello\n") }
EOF
cat >"$work/code.py" <<'PY'
def main():
    raise ValueError("boom")
main()
PY
printf 'alpha\nbeta needle gamma\ndelta\n' >"$work/notes/notes.txt"
printf 'a file with spaces\n' >"$work/my file.txt"
cat >"$work/tests/test_links.py" <<'PY'
def test_scan():
    assert 1 == 2
PY
# cargo / tsc style output, printed by a script so the run needs no rust or node.
cat >"$work/fakebuild.sh" <<'EOF'
echo "error[E0425]: cannot find value \`x\` in this scope"
echo "  --> src/main.cpp:3:24"
echo "src/main.cpp(3,24): error TS2304: Cannot find name 'x'."
EOF
chmod +x "$work/fakebuild.sh"

Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window "$win" "$out/implementer-$engine-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
place() { xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950; xdotool windowfocus "$win"; sleep 1; }

engine_flag=--engine=$engine
"$build/relay" "$engine_flag" --workspace "$work" >"$out/$engine-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
# Relay owns several X windows (a 1x1 helper and an 8x19 one); the biggest visible one is
# the real window. Root-window captures come back black on this host, so every shot is of it.
win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
        wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
        echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
      done | sort -rn | head -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
echo "window $win"
place
xdotool mousemove 500 400
sleep 2

# F12 hands the keyboard to the terminal so the run can type commands.
k F12; sleep 1
t "cd '$work'"; k Return; sleep 1
t 'clear; ls'; k Return; sleep 1.5
shot 01-ls-output

t 'grep -n needle notes/notes.txt'; k Return; sleep 1.2
t 'python3 code.py'; k Return; sleep 2
shot 02-grep-and-traceback

t './fakebuild.sh'; k Return; sleep 1.2
t 'ls -l my\ file.txt'; k Return; sleep 1.2
shot 03-cargo-tsc-and-spaces

# Cell geometry of the default font at this window size, measured from shot 03. The mouse
# steps happen before F12 goes back to the prompt box, so the rows are still where shot 03
# shows them. KonsolePart's grid sits 6 px further left with 20 px rows.
if [[ $engine == konsole ]]; then
  gx=18; gy=83; gh=20
  # Konsole 23.08's own file filter does not underline the `:line:column` suffix, so the
  # Konsole check Ctrl+clicks a plain path: `code.py` on the `python3 code.py` line.
  hrow=5; hcol=10
else
  gx=24; gy=84; gh=19
  # The Relay engine handles the suffix: `src/main.cpp:3:24` in the cargo-style error.
  hrow=15; hcol=8
fi
cellx() { python3 -c "print(int($gx + $1 * 9.03 + 4))"; }
celly() { python3 -c "print(int($gy + $1 * $gh + $gh / 2))"; }

# Hover: the pointer over the path underlines it and the tooltip names the resolved target.
# The tooltip is its own override-redirect X window, so it is captured separately (a capture
# of the Relay window alone cannot show it).
hx=$(cellx $hcol); hy=$(celly $hrow)
xdotool mousemove "$hx" "$hy"; sleep 2.5
shot 04-hover-underline
# The tooltip is the only "relay" child window that is not parked at +0+0.
tip=$(xwininfo -root -children 2>/dev/null | awk '/"relay":/ && $NF != "+0+0" {print $1; exit}')
[[ -n ${tip:-} ]] && import -window "$tip" "$out/implementer-$engine-05-hover-tooltip.png"
xwininfo -root -children >"$out/$engine-hover-windows.txt" 2>&1

# Plain click on the folder `notes` in the ls row (row 0): it opens an explorer pane.
xdotool mousemove "$(cellx 42)" "$(celly 0)" click 1
sleep 3
shot 06-click-folder-opened-explorer

# Ctrl+click the cargo error: the file opens in a preview pane at line 3.
place
xdotool keydown ctrl; xdotool mousemove "$hx" "$hy" click 1; xdotool keyup ctrl
sleep 3
shot 07-ctrl-click-opened-preview

# Back to the prompt box: the normal state, and the one the keyboard walk must work in.
xdotool mousemove 200 830 click 1; sleep 1
k F12; sleep 1.5
place
shot 08-prompt-box-focused

# Keyboard walk: Ctrl+Shift+L highlights the newest link, Up steps back, Enter opens.
k ctrl+shift+l; sleep 1.5
shot 09-link-walk-first
k Up; sleep 1; k Up; sleep 1; k Up; sleep 1.5
shot 10-link-walk-stepped-back
k Return; sleep 3
shot 11-link-walk-opened
place
sleep 1
shot 12-final
echo "screenshots in $out ($engine)"
