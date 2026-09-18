#!/usr/bin/env bash
# Probe the full provider-configuration chain: palette -> Settings » Models -> "Advanced provider
# settings" -> BYOK dialog -> type placeholder key -> OCR-click consent + Save -> dialog closes.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=$root/build
port=8739

for n in $(seq 210 230); do
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
shot() { import -window root "$out/probe-$1.png"; }

word_xy() {  # word_xy <png> <word> [first|last] -> "x y"
    local png=$1 word=$2 mode=${3:-first} found=""
    for variant in plain inverted; do
        local img=$png
        [[ $variant == inverted ]] && { convert "$png" -resize 150% -colorspace Gray -negate "$png.inv.png"; img=$png.inv.png; }
        found=$(tesseract "$img" - tsv 2>/dev/null | awk -F'\t' -v w="$word" -v mode="$mode" -v inv="$variant" '
            $1==5 && $12==w && $11+0>=30 {
                if (mode=="last") { if ($8+0>=max) {max=$8+0; x=$7+$9/2; y=$8+$10/2} }
                else if (!seen) {seen=1; x=$7+$9/2; y=$8+$10/2}
            }
            END { if (seen || max+0>0) printf "%d %d", (inv=="inverted"? x/1.5 : x), (inv=="inverted"? y/1.5 : y) }')
        [[ $variant == inverted ]] && rm -f "$png.inv.png"
        [[ -n $found ]] && { echo "$found"; return 0; }
    done
    return 1
}

"$build/relay" --workspace "$work" >"$out/probe-relay-stderr.log" 2>&1 & relay_pid=$!
sleep 6
win=$(xdotool search --pid "$relay_pid" --name '^Relay ' | head -1)
xdotool windowmove "$win" 0 0 windowsize "$win" 1400 800
xdotool windowfocus "$win"; xdotool mousemove 700 400; sleep 2

# palette -> Settings » Models
k ctrl+shift+a; sleep 1
t 'models'; sleep 1
k Return; sleep 1          # open the Models settings submenu
t 'advanced'; sleep 1
k Return; sleep 2          # run "Advanced provider settings" -> BYOK dialog
shot 3-dialog-open

# placeholder key (preset -> base -> model -> key; Extra JSON swallows Tab below it)
k Tab Tab Tab; sleep 0.3
t 'loopback-stub-not-a-key'; sleep 0.5
shot 4-key-typed

xy=$(word_xy "$out/probe-3-dialog-open.png" Send first) || { echo "OCR lost consent"; exit 1; }
echo "consent at $xy"
xdotool mousemove --sync ${xy% *} ${xy#* } click 1; sleep 0.5
# Return fires the dialog's default button (Save); the button row itself is theme-styled and
# invisible to OCR, so it is never clicked directly.
k Return; sleep 4
shot 5-after-save
xdotool windowfocus "$win"
echo done
