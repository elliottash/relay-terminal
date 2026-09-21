#!/usr/bin/env bash
# A hash copy in the Switchboard toasts, beside the notice (card #Y2F4: "when you copy to keyboard
# in the switchboard from clicking on a hash code, send a notification "#xxxx copied" (like the
# highlight-text copy notification)"; the owner's choice from three options was "Add the toast
# too"). #3ZAP built the copy itself and its "Copied #…" notice; this drive re-runs those clicks
# and checks the extra popup — a `toast`-named label at the pane's bottom-right, reading
# "#bug copied", up for 1.6 s like a terminal pane's copy-on-highlight toast and gone after.
#
# A board of two fixture cards under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR
# and TMPDIR (short paths: the 108-byte socket limit) and RELAY_KEYRING=off. Bravo carries the
# label `bug` and a body with `#bug`, a reference to Alpha's real id, a missing `#ZZZZ`, and a
# Markdown link; its thread says `#bug` again and repeats the reference. Every surface is clicked
# in turn and checked three ways: the "Copied #…" notice is read back by OCR of the screenshot
# (tesseract word boxes), the clipboard is read with xclip, and the reference click is checked by
# the other card's title appearing with the clipboard untouched.
#
#   docs/qa_evidence/2026-09-21-switchboard-hash-toast/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-21-switchboard-hash-toast}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-tag.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.local/share" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
CONF

work=$HOME/project
mkdir -p "$work"
alpha_id=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B

work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
    "columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
alpha = B.new_card("work", "Alpha plain card", "inbox", created="2026-09-01", rank="c",
                   card_id="KAN3",
                   request="a plain card that only carries a label", labels=["feature"])
B.write_new_card(board, alpha, "features")
body = (f"A bug #bug and a ref #{alpha.id.upper()}; the missing #ZZZZ reads as a label, "
        f"and [a real link](https://example.com/x) stays a link.")
bravo = B.new_card("work", "Bravo tag surfaces", "inbox", created="2026-09-10", rank="a",
                   request=body, labels=["bug"])
B.write_new_card(board, bravo, "features")
board.append_thread(bravo.id, f"Also #bug here, and the ref #{alpha.id.upper()} again.",
                    author="owner", kind="comment")
print(alpha.id.upper())
FIX
)
[[ -n $alpha_id ]] || { echo "fixture failed"; exit 1; }
echo "fixture: alpha=#$alpha_id"

(cd "$work" && exec "$build/relay" --workspace "$work" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 9
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"
xdotool windowfocus "$win"; sleep 6

# A first run can open the approvals pane; take its recommended answer to get it out of the way.
xdotool mousemove 906 513 click 1; sleep 2

shot() { import -window root "$out/$1.png"; }
board_x=690
words() { convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
              | tesseract - stdout --psm 11 tsv 2>/dev/null; }
word_box() {  # image word -> "left top width height"; a leading # is stripped, so "#AB12" is "AB12"
    words "$1" | awk -v want="$2" -v ox="$board_x" '{w=$12; sub(/^#/, "", w)}
        tolower(w)==tolower(want) {print int($7/2)+ox, int($8/2), int($9/2), int($10/2); exit}'
}
# The last match: the section checkboxes above the list say INBOX too, and the folded row to
# click is the lower one of the two.
word_box_last() {
    words "$1" | awk -v want="$2" -v ox="$board_x" '{w=$12; sub(/^#/, "", w)}
        tolower(w)==tolower(want) {box=int($7/2)+ox" "int($8/2)" "int($9/2)" "int($10/2)}
        END {print box}'
}
word_y() {  # image word -> its y center
    local box; box=$(word_box "$1" "$2")
    [[ -n $box ]] && echo $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
}
word_x() {  # image word -> its x center
    local box; box=$(word_box "$1" "$2")
    [[ -n $box ]] && echo $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 ))
}
# The box of a `bug` token on the same line as an anchor word (±8 px): the meta, body and thread
# each say #bug once, and their lines are told apart by the words around them.
bug_on_line() {  # image anchor-word -> "x y" of the #bug token on that line
    local y box
    y=$(word_y "$1" "$2") || return 1
    box=$(words "$1" | awk -v y="$y" -v ox="$board_x" '{w=$12; sub(/^#/, "", w)}
        tolower(w)=="bug" && $8/2 > y-8 && $8/2 < y+8 {print int($7/2)+ox+int($9/4), int($8/2)+int($10/4); exit}')
    [[ -n $box ]] && echo "$box"
}
hashtag_on_line() {  # image anchor-word -> "x y" of the `#bug` token, `#` and all, on that line
    local y box
    y=$(word_y "$1" "$2") || return 1
    box=$(words "$1" | awk -v y="$y" -v ox="$board_x" '{
        w=$12; if (substr(w,1,1) != "#") next; sub(/^#/, "", w)
        if (tolower(w) == "bug" && $8/2 > y-8 && $8/2 < y+8) {
            print int($7/2)+ox+int($9/2), int($8/2)+int($10/4); exit } }')
    [[ -n $box ]] && echo "$box"
}
line_text() {  # png -> its whole text, from the same upscale
    convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 2>/dev/null
}
click_at() {  # x y [button] — a missing position is logged, never clicked
    if [[ -z ${1:-} || -z ${2:-} ]]; then echo "FAIL click_at without a position" >>"$out/ocr.txt"; return 1; fi
    xdotool mousemove "$1" "$2" click "${3:-1}"; sleep 1.8; }
toast_shot() {  # name x y — click and shoot while the 1.6 s toast is still up (3 tries: the
                # whole-screen grab itself takes long enough to miss the popup now and then)
    local name=$1 x=$2 y=$3 tries=0
    if [[ -z ${x:-} || -z ${y:-} ]]; then echo "FAIL toast_shot without a position" >>"$out/ocr.txt"; return 1; fi
    while (( tries < 3 )); do
        xdotool mousemove "$x" "$y" click 1
        sleep 0.35
        import -window root "$out/$name.png"
        line_text "$out/$name.png" | grep -qi "#bug copied" && return 0
        tries=$((tries + 1))
    done
    return 1; }
clip() { xclip -o -selection clipboard 2>/dev/null; }
clip_clear() { xclip -i -selection clipboard </dev/null 2>/dev/null; sleep 0.3; }
pass=0 fail=0
check() {  # name expected actual
    if [[ "$2" == "$3" ]]; then echo "PASS $1: '$3'" >>"$out/ocr.txt"; pass=$((pass+1));
    else echo "FAIL $1: expected '$2' got '$3'" >>"$out/ocr.txt"; fail=$((fail+1)); fi
}
notice_has() {  # name png want
    if line_text "$2" | grep -qi "$3"; then echo "PASS $1 (ocr '$3')" >>"$out/ocr.txt"; pass=$((pass+1));
    else echo "FAIL $1: no '$3' in shot $2" >>"$out/ocr.txt"; fail=$((fail+1)); fi
}
toast_has() {  # name png want — the copy toast, up for 1.6 s after the click
    if line_text "$2" | grep -qi "$3"; then echo "PASS $1 (ocr '$3')" >>"$out/ocr.txt"; pass=$((pass+1));
    else echo "FAIL $1: no '$3' in shot $2" >>"$out/ocr.txt"; fail=$((fail+1)); fi
}
toast_gone() {  # name png — the popup has faded; the notice's own wording may still be there
    if line_text "$2" | grep -qi "#bug copied"; then
        echo "FAIL $1: toast still up in $2" >>"$out/ocr.txt"; fail=$((fail+1));
    else echo "PASS $1 (no '#bug copied')" >>"$out/ocr.txt"; pass=$((pass+1)); fi
}

: >"$out/ocr.txt"

# 1. Open the Switchboard and unfold Inbox, where both fixture cards sit.
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 00-folded
box=$(word_box_last "$out/00-folded.png" "INBOX")
[[ -n $box ]] && click_at $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )) \
                         $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
shot 01-board-open

# 2. The row badge: a click on Bravo's `bug` badge copies #bug, notice and all, without opening
#    the card (the shot still shows the list).
pos=$(bug_on_line "$out/01-board-open.png" "Bravo")
echo "02 bravo badge at: $pos" >>"$out/ocr.txt"
toast_shot 02-badge-click ${pos% *} ${pos#* }
notice_has "02 badge notice" "$out/02-badge-click.png" "Copied #bug"
toast_has "02 badge toast" "$out/02-badge-click.png" "#bug copied"
check "02 badge clipboard" "#bug" "$(clip)"
check "02 list still shown (card not opened)" "yes" "$(words "$out/02-badge-click.png" | grep -q Bravo && echo yes)"

# 3. Open Bravo; the meta labels, the body and the thread each carry a #bug.
click_at "$(word_x "$out/02-badge-click.png" Bravo)" "$(word_y "$out/02-badge-click.png" Bravo)"
sleep 2
shot 03-bravo-open
notice_has "03 body rendered" "$out/03-bravo-open.png" "ref"

# 4. The card-top meta label (#bug beside `labels`) copies.
pos=$(hashtag_on_line "$out/03-bravo-open.png" "labels")
echo "04 meta #bug at: $pos" >>"$out/ocr.txt"
clip_clear
toast_shot 04-meta-click ${pos% *} ${pos#* }
notice_has "04 meta notice" "$out/04-meta-click.png" "Copied #bug"
toast_has "04 meta toast" "$out/04-meta-click.png" "#bug copied"
check "04 meta clipboard" "#bug" "$(clip)"

# 5. The body hashtag (the #bug on the line with `and`) copies.
pos=$(hashtag_on_line "$out/03-bravo-open.png" "missing")
echo "05 body #bug at: $pos" >>"$out/ocr.txt"
clip_clear
toast_shot 05-body-click ${pos% *} ${pos#* }
notice_has "05 body notice" "$out/05-body-click.png" "Copied #bug"
toast_has "05 body toast" "$out/05-body-click.png" "#bug copied"
check "05 body clipboard" "#bug" "$(clip)"

# 6. The thread hashtag (the #bug on the line with `Also`) copies.
pos=$(hashtag_on_line "$out/03-bravo-open.png" "Also")
echo "06 thread #bug at: $pos" >>"$out/ocr.txt"
clip_clear
toast_shot 06-thread-click ${pos% *} ${pos#* }
notice_has "06 thread notice" "$out/06-thread-click.png" "Copied #bug"
toast_has "06 thread toast" "$out/06-thread-click.png" "#bug copied"
check "06 thread clipboard" "#bug" "$(clip)"
# The toast is transient: 2.8 s after the click it is gone, while the notice (10 s) still reads.
sleep 2.4
shot 06b-toast-gone
toast_gone "06b toast faded" "$out/06b-toast-gone.png"
notice_has "06b notice still up" "$out/06b-toast-gone.png" "Copied #bug"

# 7. The reference: a click on Alpha's id in the body zooms to Alpha, clipboard untouched.
xpos=$(word_x "$out/03-bravo-open.png" "$alpha_id"); ypos=$(word_y "$out/03-bravo-open.png" "$alpha_id")
echo "07 ref #$alpha_id at: $xpos $ypos" >>"$out/ocr.txt"
click_at "$xpos" "$ypos"
sleep 2
shot 07-ref-click
check "07 clipboard untouched by the ref" "#bug" "$(clip)"
check "07 zoomed to Alpha's title" "yes" "$(words "$out/07-ref-click.png" | grep -q Alpha && echo yes)"

echo "---- $pass passed, $fail failed ----" | tee -a "$out/ocr.txt"
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
[[ $fail -eq 0 ]]
