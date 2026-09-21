#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# #97EG, live: a real Relay desktop in a jail of its own under Xvfb makes a meeting code and PIN
# in its share window ("Make a code"); a real Chrome on the same display opens the join page the
# desktop's own sidecar serves, types the code and PIN, runs CPace in the page, and knocks; the
# owner's Sharing pane shows the knock row with its five-digit code, checked against the guest's,
# and admits. There is no window manager on the Xvfb display, so windows are focused with
# xdotool windowfocus, stacking is done by moving the other window off-screen, and buttons are
# found by OCR (tesseract TSV) and clicked by coordinates. Writes implementer-live-*.png beside
# itself.
#
#   docs/qa_evidence/2026-09-18-join-with-meeting-code-and-pin/live.sh [build dir]
set -uo pipefail
export RELAY_KEYRING=off
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
node_bin=$(command -v node)
chrome_bin=$(command -v google-chrome || command -v chromium || command -v chromium-browser)
[[ -x $build/relay ]] || { echo "no relay binary in $build"; exit 1; }
[[ -n $chrome_bin ]] || { echo "no Chrome on this machine"; exit 1; }
d=; for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { d=:$n; break; }; done
[[ -z $d ]] && { echo "no free X display"; exit 1; }
state=$(mktemp -d)
pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null; sleep 1; rm -rf "$state" 2>/dev/null; }
trap cleanup EXIT

j=$state/B
mkdir -p "$j/home" "$j/config/RelayTerminal" "$j/data" "$j/cache" "$j/work" "$j/tmp"
mkdir -m 700 -p "$j/run"
printf '[instructions]\nonboarded=true\n' >"$j/config/RelayTerminal/relay.conf"

Xvfb "$d" -screen 0 1500x950x24 >/dev/null 2>&1 & pids+=($!)
for _ in $(seq 1 60); do DISPLAY=$d xdpyinfo >/dev/null 2>&1 && break; sleep 0.25; done
DISPLAY=$d xdpyinfo >/dev/null 2>&1 || { echo "Xvfb never came up on $d"; exit 1; }
( export HOME=$j/home XDG_CONFIG_HOME=$j/config XDG_DATA_HOME=$j/data XDG_CACHE_HOME=$j/cache \
    XDG_RUNTIME_DIR=$j/run TMPDIR=$j/tmp DISPLAY=$d \
    RELAY_REMOTE_CODE_FILE="$state/code.txt" RELAY_REMOTE_INVITE_FILE="$state/invite.txt"
  cd "$j/work" && exec "$build/relay" --workspace "$j/work" --clean-shell --fresh \
    >"$out/live-relay.log" 2>&1 ) &
relay_pid=$!; pids+=($relay_pid)

X() { DISPLAY=$d xdotool "$@"; }
shot() { DISPLAY=$d import -window root "$out/implementer-$1"; }
scratch() { DISPLAY=$d import -window root "$1"; }
# ocr_click <png> <word A> <word B>: click the centre of A where B sits on the same row to its
# right (a button's two words); prints nothing and returns 1 when the pair is not on screen. The
# shot is upscaled 3x for the OCR (button text is ~11px) and the coordinates divided back down.
ocr_click() {
  local xy
  convert "$1" -resize 300% "$1.3x.png" 2>/dev/null || cp "$1" "$1.3x.png"
  xy=$(tesseract "$1.3x.png" - tsv 2>/dev/null | awk -F'\t' -v a="$2" -v b="$3" '
    $11 > 25 && $12 ~ a { ax = $7; ay = $8; aw = $9 }
    $11 > 25 && $12 ~ b && ax != "" && $8 >= ay - 24 && $8 <= ay + 24 && $7 > ax && $7 < ax + 600 {
      print int((ax + (aw + $7 - ax + $9) / 2) / 3), int((ay + 24) / 3); found = 1; exit }
    END { if (!found) exit 1 }')
  [[ -n $xy ]] || return 1
  DISPLAY=$d xdotool mousemove $xy click 1
}
wait_file() { for _ in $(seq 1 "$2"); do [[ -s $1 ]] && return 0; sleep 1; done; return 1; }
wait_line() { for _ in $(seq 1 "$3"); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 1; done; return 1; }
palette() { # run one palette action on the main window, by its title's distinctive words
  X windowraise "$w" 2>/dev/null; X windowfocus "$w"; sleep 0.7
  local opened=
  for _ in 1 2 3; do
    X key ctrl+shift+a; sleep 1.4
    scratch "$state/palette-check.png"
    tesseract "$state/palette-check.png" - 2>/dev/null | grep -qi "esc closes\|enter runs\|enterruns" && { opened=yes; break; }
    sleep 1
  done
  [[ -n $opened ]] || return 1
  X type --delay 35 "$1"; sleep 1.2
  scratch "$state/palette-filter.png"
  tesseract "$state/palette-filter.png" - 2>/dev/null | grep -qi "Nothing matches" && { X key Escape; sleep 0.5; return 1; }
  X key Return; sleep 3
}

w=
for _ in $(seq 1 60); do w=$(X search --onlyvisible --name "^Relay" 2>/dev/null | tail -1); [[ -n $w ]] && break; sleep 0.5; done
[[ -z $w ]] && { echo "Relay did not open a window"; tail -5 "$out/live-relay.log"; exit 1; }
X windowmove "$w" 0 0 windowsize "$w" 1500 950 windowraise "$w" 2>/dev/null
X windowfocus "$w"; sleep 4

# A fresh profile is asked about approvals first; the modal would starve the palette's list.
scratch "$state/startup.png"
if tesseract "$state/startup.png" - 2>/dev/null | grep -qi "Allow everything"; then
  ocr_click "$state/startup.png" Allow everything || ocr_click "$state/startup.png" Allow recommended
  sleep 2
fi

# --- the share window, "Make a code" -------------------------------------------------------------
dlg=
for _ in 1 2; do
  palette "Share this pane"
  dlg=$(X search --name "Share this pane" 2>/dev/null | tail -1)
  [[ -n $dlg ]] && break
  X key Escape; sleep 1   # a palette left open
done
[[ -z $dlg ]] && { echo "the share dialog did not open"; shot live-fail-no-dialog.png; exit 1; }
sleep 3   # the sidecar starts and the QR settles
scratch "$state/before-code.png"
ocr_click "$state/before-code.png" Make code || ocr_click "$state/before-code.png" Make a \
  || { echo "could not find the Make a code button"; shot live-fail-no-button.png; exit 1; }
wait_file "$state/code.txt" 25 || { echo "no code came back"; shot live-fail-no-code.png; exit 1; }
read -r meet_code meet_pin <"$state/code.txt"
echo "code ready: $meet_code (the PIN is on screen, and in the code QA hook only)"
sleep 1
shot live-01-desktop-code-and-pin.png
X windowmove "$dlg" 950 80 2>/dev/null   # parked: visible, off the Sharing pane's rows

# Without a window manager a raised window stays on top, so Chrome is moved off-screen when the
# desktop needs the display and the keyboard. The guest's own shots come from the driver over
# the DevTools protocol: X captures of a Chrome window on a bare Xvfb come out blank.
cw=
show_relay() {
  [[ -n $cw ]] && X windowmove "$cw" 2000 0 2>/dev/null
  X windowmove "$w" 0 0 2>/dev/null; X windowraise "$w" 2>/dev/null
  X windowmove "$dlg" 950 80 2>/dev/null
  sleep 1
}

# --- the Sharing pane, open before anybody knocks ------------------------------------------------
palette "Sharing"
scratch "$state/sharing-empty.png"
grep -q "sharing" <(tesseract "$state/sharing-empty.png" - 2>/dev/null | tr 'A-Z' 'a-z') \
  || echo "note: the Sharing pane may not be focused; the knock row check below decides"

# --- the sidecar's own join page ------------------------------------------------------------------
sidecar_port=
for _ in $(seq 1 20); do
  spid=$(pgrep -P "$relay_pid" -f "remote.gui_host" | head -1)
  [[ -n $spid ]] && sidecar_port=$(ss -tlnp 2>/dev/null | grep "pid=$spid," \
    | grep -oE '127\.0\.0\.1:[0-9]+' | head -1 | cut -d: -f2)
  [[ -n $sidecar_port ]] && break
  sleep 0.5
done
[[ -z $sidecar_port ]] && { echo "the sidecar's rendezvous port was not found"; exit 1; }
join_url="http://127.0.0.1:$sidecar_port/join"
echo "join page: $join_url"

# --- the guest: a real Chrome, driven over the DevTools protocol ----------------------------------
"$chrome_bin" --user-data-dir="$state/chrome" --no-first-run --no-default-browser-check \
  --disable-session-crashed-bubble --hide-crash-restore-bubble --disable-dev-shm-usage \
  --disable-gpu --remote-debugging-port=0 --window-size=1200,850 --window-position=0,0 \
  "$join_url" >/dev/null 2>&1 &
pids+=($!)
for _ in $(seq 1 30); do [[ -s $state/chrome/DevToolsActivePort ]] && break; sleep 0.5; done
[[ -s $state/chrome/DevToolsActivePort ]] || { echo "Chrome never opened its DevTools port"; exit 1; }
for _ in $(seq 1 20); do
  cw=
  for id in $(X search --class "google-chrome" 2>/dev/null); do
    gw=$(X getwindowgeometry --shell "$id" 2>/dev/null | awk -F= '/^WIDTH/{print $2}')
    [[ ${gw:-0} -gt 200 ]] && cw=$id   # beside the real window Chrome maps a 10x10 helper
  done
  [[ -n $cw ]] && break
  sleep 0.5
done
"$node_bin" "$out/drive_guest.mjs" "$state/chrome" "$join_url" "$meet_code" "$meet_pin" "Sam (live)" \
  "$out/implementer-live-" >"$state/guest.jsonl" 2>"$out/live-guest.log" &
pids+=($!)

wait_line "$state/guest.jsonl" '"stage":"form"' 60 \
  || { echo "the code form never came up"; cat "$state/guest.jsonl"; exit 1; }

wait_line "$state/guest.jsonl" '"stage":"knocking"' 120 \
  || { echo "the guest never knocked"; cat "$state/guest.jsonl"; shot live-fail-never-knocked.png; exit 1; }
knock_code=$(grep -o '"code":"[0-9]*"' "$state/guest.jsonl" | head -1 | grep -o '[0-9]*')
echo "guest is knocking; their screen shows $knock_code"

# --- the owner: the knock row on the Sharing pane, the code compared, then Admit ------------------
show_relay
admitted=
for _ in $(seq 1 20); do
  scratch "$state/knockrow.png"
  if tesseract "$state/knockrow.png" - 2>/dev/null | grep -oE '\b[0-9]{5}\b' | grep -qx "$knock_code"; then
    echo "the five-digit codes match on both screens: $knock_code"
    break
  fi
  sleep 2
done
for click in 1 2 3; do
  [[ $click -gt 1 ]] && { scratch "$state/knockrow.png"; sleep 1; }
  ocr_click "$state/knockrow.png" Admit viewer || { sleep 2; continue; }
  for _ in $(seq 1 10); do
    grep -q '"stage":"joined"' "$state/guest.jsonl" 2>/dev/null && { admitted=yes; break; }
    sleep 2
    scratch "$state/admit-check.png"
    # The driver's socket can die as the session begins; the desktop saying "Here now" is the
    # same fact from the other side.
    tesseract "$state/admit-check.png" - 2>/dev/null | grep -qi "here now" && { admitted="yes (desktop side)"; break; }
    grep -q '"stage":"refused"' "$state/guest.jsonl" 2>/dev/null && break
  done
  [[ -n $admitted ]] && break
done
[[ -z $admitted ]] && { echo "the guest was never admitted"; cat "$state/guest.jsonl"; shot live-fail-not-admitted.png; exit 1; }
echo "admitted: $(tail -1 "$state/guest.jsonl")"
sleep 2
show_relay
shot live-05-desktop-guest-here-now.png
X windowraise "$dlg" 2>/dev/null; sleep 1
shot live-06-desktop-code-used.png

# --- what the machine says ------------------------------------------------------------------------
fail=0
check() { if eval "$2"; then echo "ok: $1"; else echo "FAIL: $1"; fail=1; fi; }
audit=$(find "$j/data" -type f -path "*remote*" 2>/dev/null | tr '\n' ' ')
check "audit recorded code_create and code_used" \
  "grep -qh code_create $audit && grep -qh code_used $audit"
check "audit recorded the knock and the admission" \
  "grep -qh knock $audit && grep -qh admitted $audit"
check "the PIN is nowhere in the desktop's stores or logs" \
  "! grep -qar '$meet_pin' '$j/data' '$j/config' '$out/live-relay.log'"
check "the PIN reached nothing but the code QA hook" "grep -q '$meet_pin' '$state/code.txt'"
echo "--- audit lines for the code ---"
grep -h "code_\|knock\|admitted" $audit 2>/dev/null | tail -8
echo "--- guest stages ---"; cat "$state/guest.jsonl"
exit $fail
