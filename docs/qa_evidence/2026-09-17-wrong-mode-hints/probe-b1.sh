#!/usr/bin/env bash
# Replicate drive.sh launch B + B1 with rapid screenshots to see the wrong-mode toast lifecycle.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=$root/build
port=8741

for n in $(seq 240 260); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export RELAY_KEYRING=off
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" "${stub_pid:-0}" 2>/dev/null' EXIT

pkill -f "stub-provider.py $port" 2>/dev/null; sleep 0.3
python3 "$out/stub-provider.py" "$port" & stub_pid=$!
sleep 1
Xvfb "$display" -screen 0 1400x800x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display

t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }
shot() { import -window root "$out/b1-$1.png"; }

word_xy() {
    local png=$1 word=$2 found=""
    for variant in plain inverted; do
        local img=$png
        [[ $variant == inverted ]] && { convert "$png" -resize 150% -colorspace Gray -negate "$png.inv.png"; img=$png.inv.png; }
        found=$(tesseract "$img" - tsv 2>/dev/null | awk -F'\t' -v w="$word" -v inv="$variant" '
            $1==5 && $12==w && $11+0>=30 && !seen {seen=1; x=$7+$9/2; y=$8+$10/2}
            END { if (seen) printf "%d %d", (inv=="inverted"? x/1.5 : x), (inv=="inverted"? y/1.5 : y) }')
        [[ $variant == inverted ]] && rm -f "$png.inv.png"
        [[ -n $found ]] && { echo "$found"; return 0; }
    done
    return 1
}

"$build/relay" --workspace "$work" >"$out/b1-relay-stderr.log" 2>&1 & relay_pid=$!
sleep 6
win=$(xdotool search --pid "$relay_pid" --name '^Relay ' | head -1)
xdotool windowmove "$win" 0 0 windowsize "$win" 1400 800
xdotool windowfocus "$win"; xdotool mousemove 700 400; sleep 2

# configure (same chain as drive.sh)
k ctrl+shift+a; sleep 1
t 'models'; sleep 1
k Return; sleep 1
t 'advanced'; sleep 1
k Return; sleep 2
k Tab Tab Tab; sleep 0.3
t 'loopback-stub-not-a-key'; sleep 0.5
shot dlg
xy=$(word_xy "$out/b1-dlg.png" Send) || { echo "OCR lost consent"; exit 1; }
xdotool mousemove --sync ${xy% *} ${xy#* } click 1; sleep 0.5
k Return; sleep 4
xdotool windowfocus "$win"; sleep 1

# B1 with rapid shots
k ctrl+i; sleep 1.5
t 'find the largest files'; k Return
shot 00-0s; sleep 0.4
shot 01-04s; sleep 0.4
shot 02-08s; sleep 0.4
shot 03-12s; sleep 1
shot 04-21s; sleep 2
shot 05-42s; sleep 3
shot 06-72s
echo done
