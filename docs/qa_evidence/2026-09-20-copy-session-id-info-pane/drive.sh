#!/usr/bin/env bash
# The ⓘ pane's copy affordance (#YQC3): implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1 (the harness of the transcript-gap run, #5AWD,
# with this card's scenes — the stub spawns one real subagent thread so the thread page is real).
#
#   docs/qa_evidence/2026-09-20-copy-session-id-info-pane/drive.sh [build-dir]
#
# Scenes, as implementer-<n>-<name>.png (+ OCR and the clipboard's exact bytes in
# implementer-notes.txt):
#   01-noid        /status before any turn: a Session row with no id, no ⧉ and no dead link
#   02-id          after the turn: the Session row shows the id and the ⧉ icon; -zoom.png is a
#                  300 % crop of that row, for the glyph itself (joined squares, not a tofu box)
#   03-copy-id     click the id text: the clipboard equals the saved session file's id byte for
#                  byte (xclip -o), and the hint line flashes "Copied session id …"
#   04-restored    2.5 s later the standing hint line is back
#   05-copy-icon   click the ⧉ icon right of the id: the same exact id again, flash again
#   06-thread      the history's thread link opens the thread page (Thread row: id + ⧉)
#   07-copy-thread click the thread page's ⧉: the clipboard equals that thread's id, flash says
#                  "Copied thread id …"
#
# Needs Xvfb, xdotool, ImageMagick, tesseract, xclip.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1200 height=900
port=${RELAY_QA_PORT:-8817}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-copyid.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[security]
approvals_chosen=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[provider]
preset=local:stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 1.5

notes=$out/implementer-notes.txt
rm -f "$notes"

snap() {   # shoot at once — used inside the hint's two-second flash window
    import -window "$win" "$out/implementer-$1.png"
}
ocr_flash() {   # the bottom strip of a shot, where the hint line lives, into the notes
    convert "$out/implementer-$1.png" -crop ${width}x140+0+$((height - 140)) +repage -scale 200% "$sandbox/hint-$1.png" 2>/dev/null
    { echo "--- $1 (hint line)"; tesseract "$sandbox/hint-$1.png" - --psm 6 2>/dev/null; } >>"$notes"
}
fail() {   # leave the logs behind before giving up
    echo "FAIL: $1" | tee -a "$notes"
    cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
    mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
    exit 1
}
shot() {    # park the mouse, shoot, keep a 150 % crop of the body and OCR it into the notes
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x$height+0+0 +repage -scale 150% "$out/implementer-$1-body.png"
    { echo "--- $1 (body)"; tesseract "$out/implementer-$1-body.png" - --psm 6 2>/dev/null; } >>"$notes"
}
ask() { t "$1"; k Return; }
copied() { timeout 5 xclip -o -selection clipboard 2>/dev/null; }

# OCR the info pane — the right side of the window; print "left top width height" of the first
# word matching the regex, in window coordinates (reading order: the first is highest). Cropping
# to the pane keeps the transcript's words (another "general", another hex id) out. A 32-hex id
# often OCRs with case slips or split in two, so the locator regexes are loose (exact bytes come
# from the saved session file) and a second pass rescales before giving up.
ocr_word() {   # $1 = awk regex the whole word must match
    local px=560 box scale
    import -window "$win" "$sandbox/page.png" 2>/dev/null
    convert "$sandbox/page.png" -crop ${px}x$height+$((width - px))+0 +repage "$sandbox/pane.png" 2>/dev/null
    for scale in 150 220; do
        convert "$sandbox/pane.png" -scale ${scale}% "$sandbox/page15.png" 2>/dev/null
        tesseract "$sandbox/page15.png" "$sandbox/page" --psm 6 tsv >/dev/null 2>&1
        box=$(awk -F'\t' -v re="$1" -v s=$scale -v ox=$((width - px)) \
            '$12 != "" && $12 ~ re {print int($7 * 100 / s + ox), int($8 * 100 / s), int($9 * 100 / s), int($10 * 100 / s); exit}' \
            "$sandbox/page.tsv")
        [[ -n $box ]] && { echo "$box"; return 0; }
    done
    return 1
}

# Click near a token until the clipboard holds $expect (each x in turn; misses change nothing).
# Prints the x that worked. The click happens on whichever candidate hits, so callers snap after.
click_copy() {   # $1=expect  $2=y  $3..=x candidates
    local expect=$1 y=$2 x; shift 2
    for x in "$@"; do
        xdotool mousemove "$x" "$y" click 1; sleep 0.6
        [[ $(copied) == "$expect" ]] && { echo "$x"; return 0; }
    done
    return 1
}

hex32='^[0-9a-f]{32}$'          # exact ids (the clipboard and the saved files must match this)
loose32='^[0-9a-zA-Z]{32}$'     # OCR locator: case slips are fine, position is all we need

# The ⧉ cluster inside an OCR'd box: scan the row's pixels for ink columns, split them into
# clusters, and print "centre first last" of the last small one — the icon after the id text.
# (Clicking past a line's last char can still trigger its anchor — Qt snaps the cursor back — so
# the evidence click should land on the glyph's own pixels, not the blank after it.)
glyph_x() {   # $1=image  $2..5=box left top width height
    python3 - "$1" "$2" "$3" "$4" "$5" <<'PY'
import re, subprocess, sys
img, ix, iy, iw, ih = sys.argv[1], *map(int, sys.argv[2:6])
raw = subprocess.run(["convert", img, "-crop", f"{iw + 44}x{ih + 10}+{ix - 2}+{iy - 5}", "+repage",
                      "-colorspace", "Gray", "txt:-"], capture_output=True, text=True).stdout
cols = {}
for line in raw.splitlines():
    if ":" not in line or "(" not in line: continue
    xy, val = line.split(":", 1)
    x = int(xy.split(",")[0])
    cols.setdefault(x, []).append(float(val.strip(" ()\n").split(",")[0]))
if not cols:
    sys.exit(1)
peaks = sorted(max(c) for c in cols.values())
bg = peaks[len(peaks) // 5]                      # the row's background brightness
ink = [x for x in sorted(cols) if max(cols[x]) > bg + 40]
clusters, run = [], []
for x in ink:
    if run and x - run[-1] > 3:
        clusters.append(run); run = []
    run.append(x)
clusters.append(run)
small = [c for c in clusters if 3 <= len(c) <= 16] or clusters
c = small[-1]
print(ix - 2 + (c[0] + c[-1]) // 2, ix - 2 + c[0], ix - 2 + c[-1])
PY
}

echo "== build $(cat "$build/build-id" 2>/dev/null || echo '?'), commit $(git -C "$root" rev-parse --short HEAD)" >>"$notes"
echo "== font fontconfig picks for U+29C9 (⧉): $(fc-match -s ':charset=29C9' 2>/dev/null | head -1)" >>"$notes"

# 01 — before any turn: no session id yet, so no copy link at all.
ask /status; sleep 4; shot 01-noid
k Escape; sleep 1

# 02 — one turn with a real subagent thread, then the session page.
ask 'spawn one helper'; sleep 35
ask /status; sleep 4; shot 02-id
read -r ix iy iw ih < <(ocr_word "$loose32")
[[ ${iw:-0} -gt 0 ]] || fail "no 32-hex id found on the page"
echo "== id token box (window px): $ix $iy $iw $ih" >>"$notes"
convert "$out/implementer-02-id.png" -crop $((iw + 320))x$((ih + 50))+$((ix - 260))+$((iy - 25)) +repage \
    -scale 300% "$out/implementer-02-id-zoom.png"

# The saved session file is the oracle for the exact bytes: its own id, and every other 32-hex
# token in it (the thread ids it recorded).
ids=$(python3 - "$HOME" "$work" <<'PY'
import json, pathlib, re, sys
best = None
threads = {}          # session id -> its thread ids, from the *.threads/<tid>.json layout
for root in sys.argv[1:]:
    base = pathlib.Path(root)
    for p in base.rglob('*.threads/*.json'):
        owner = p.parent.name[:-len('.threads')]
        if re.fullmatch(r'[0-9a-f]{32}', owner) and re.fullmatch(r'[0-9a-f]{32}', p.stem):
            threads.setdefault(owner, set()).add(p.stem)
    for p in base.rglob('*.json'):
        try:
            d = json.loads(p.read_text())
        except Exception:
            continue
        sid = d.get('session_id') or d.get('id')
        if not (isinstance(sid, str) and re.fullmatch(r'[0-9a-f]{32}', sid)):
            continue
        m = p.stat().st_mtime
        if best is None or m > best[0]:
            best = (m, sid, set(re.findall(r'[0-9a-f]{32}', p.read_text())))
if best:
    _, sid, tokens = best
    tokens |= threads.get(sid, set())
    print(sid, ' '.join(sorted(tokens - {sid})))
PY
)
sid=${ids%% *}; tids=${ids#* }
[[ $sid =~ $hex32 ]] || fail "no session file with an id under the sandbox"
echo "== saved session id: $sid; thread ids: ${tids:-none}" >>"$notes"

# 03 — click the id text itself: the whole id, byte for byte.
hit=$(click_copy "$sid" $((iy + ih / 2)) $((ix + iw / 2))) || fail "clicking the id copied nothing"
echo "== click on the id text (x=$hit) put on the clipboard: $(copied)" >>"$notes"
snap 03-copy-id; ocr_flash 03-copy-id

# 04 — the standing hint line comes back on its own.
sleep 2.5; shot 04-restored; ocr_flash 04-restored

# 05 — click the ⧉ icon right of the id, on the glyph's own pixels (then offsets as fallback).
glyph=$(glyph_x "$sandbox/page.png" "$ix" "$iy" "$iw" "$ih") || glyph=
gxc=${glyph%% *}
echo "== ⧉ ink cluster: ${glyph:-not found} (id box ends at $((ix + iw)))" >>"$notes"
hit=$(click_copy "$sid" $((iy + ih / 2)) ${gxc:-$((ix + iw + 14))} $((ix + iw + 14)) $((ix + iw + 10)) $((ix + iw + 18)) $((ix + iw + 7))) \
    || fail "clicking the ⧉ icon copied nothing"
echo "== click on the ⧉ icon (x=$hit) put on the clipboard: $(copied)" >>"$notes"
snap 05-copy-icon; ocr_flash 05-copy-icon
sleep 2.5

# 06 — the thread link in the history opens the thread page.
read -r gx gy gw gh < <(ocr_word '^general$')
[[ ${gw:-0} -gt 0 ]] || { read -r gx gy gw gh < <(ocr_word '[Pp]inenut'); }
[[ ${gw:-0} -gt 0 ]] || fail "no thread link (word 'general') found"
xdotool mousemove $((gx + gw / 2)) $((gy + gh / 2)) click 1; sleep 3; shot 06-thread

# 07 — the thread page's Thread id copies too: the ⧉ glyph itself first, then fallbacks.
read -r tx ty tw th < <(ocr_word "$loose32")
[[ ${tw:-0} -gt 0 ]] || fail "no thread id found on the thread page"
glyph=$(glyph_x "$sandbox/page.png" "$tx" "$ty" "$tw" "$th") || glyph=
gxc=${glyph%% *}
echo "== thread ⧉ ink cluster: ${glyph:-not found} (id box ends at $((tx + tw)))" >>"$notes"
tid=$(echo "$tids" | tr ' ' '\n' | grep -E "$hex32" | head -1)
copy_ok() {
    local c; c=$(copied)
    [[ $c =~ $hex32 ]] || return 1
    if [[ -n $tids ]]; then [[ " $tids " == *" $c "* ]]; else [[ $c != "$sid" ]]; fi
}
hit=
for x in ${gxc:-$((tx + tw + 14))} $((tx + tw + 14)) $((tx + tw + 10)) $((tx + tw + 18)) $((tx + tw / 2)); do
    xdotool mousemove "$x" $((ty + th / 2)) click 1; sleep 0.6
    if copy_ok; then hit=$x; break; fi
done
[[ -n $hit ]] || fail "thread-page click copied no id"
if [[ -n $tid ]]; then [[ $(copied) == "$tid" ]] || fail "clipboard $(copied) != saved thread id $tid"; fi
echo "== click on the thread page (x=$hit) put on the clipboard: $(copied)" >>"$notes"
snap 07-copy-thread; ocr_flash 07-copy-thread

cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
printf 'done: %s\n' "$out"
