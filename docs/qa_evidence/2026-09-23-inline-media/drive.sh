#!/usr/bin/env bash
# #MDA7: exercise real Relay under Xvfb, with a loopback stub and an isolated profile.
# Usage: drive.sh [build-dir]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=1100
port=${RELAY_QA_PORT:-8847}
display=
for n in $(seq 560 599); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -n $display ]] || { echo 'No free X display' >&2; exit 1; }
sandbox=$(mktemp -d /tmp/relay-mda7.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $stub_pid $xvfb_pid; do [[ -z $pid ]] || kill "$pid" 2>/dev/null || true; done; for pid in $relay_pid $stub_pid $xvfb_pid; do [[ -z $pid ]] || wait "$pid" 2>/dev/null || true; done; if [[ ${RELAY_QA_KEEP:-0} == 1 ]]; then echo "kept sandbox: $sandbox"; else rm -rf "$sandbox"; fi; }
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >"$sandbox/stub.log" 2>&1 & stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >"$sandbox/xvfb.log" 2>&1 & xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid"
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"

python3 - "$work" <<'PY'
import math, pathlib, struct, sys, wave
from PIL import Image, ImageDraw
work = pathlib.Path(sys.argv[1])
with wave.open(str(work / 'tone.wav'), 'wb') as sound:
    sound.setnchannels(1); sound.setsampwidth(2); sound.setframerate(8000)
    sound.writeframes(b''.join(struct.pack('<h', int(12000*math.sin(2*math.pi*440*i/8000))) for i in range(16000)))
(work / 'numbers.csv').write_text('Item,Value\nSmall,2\nLarge,10\n')
(work / 'chart.html').write_text('<!doctype html><html><body style="background:#101827;color:#f6f7ff;font:40px sans-serif"><h1>Interactive chart</h1><svg width="900" height="400"><polyline points="60,320 300,80 550,240 820,30" fill="none" stroke="#75caff" stroke-width="12"/></svg><p>Click the snapshot to open this live page.</p></body></html>')
(work / 'demo.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="900" height="300"><rect width="900" height="300" fill="#1c314a"/><text x="70" y="185" fill="#ffbd6b" font-size="90">SVG preview</text></svg>')
frames = []
for color in ('#8844dd', '#ff8844'):
    image = Image.new('RGB', (480, 150), color)
    ImageDraw.Draw(image).text((30, 50), 'Animated GIF', fill='white')
    frames.append(image)
frames[0].save(work / 'animate.gif', save_all=True, append_images=frames[1:], duration=250, loop=0)
frames[0].save(work / 'animate.webp', save_all=True, append_images=frames[1:], duration=250, loop=0)
PY
printf '\\documentclass{article}\n\\begin{document}\n\\Huge PDF preview\\end{document}\n' > "$work/demo.tex"
(cd "$work" && pdflatex -interaction=nonstopmode -halt-on-error demo.tex >/dev/null)
ffmpeg -nostdin -v error -y -f lavfi -i testsrc=size=640x360:rate=12 -t 3 -pix_fmt yuv420p "$work/clip.mp4"

cat >"$HOME/.bashrc" <<RC
PS1='\\w \\$ '
unset PROMPT_COMMAND
if [[ ! -e ~/.media-ran ]]; then
  touch ~/.media-ran
  printf 'Sound and sortable CSV:\\n'
  relay-show '$work/tone.wav'
  relay-show '$work/numbers.csv'
fi
RC
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[models]
tier\\main=local:stub|llava-stub|high
tier\\high=local:stub|llava-stub|high
tier\\flash=local:stub|llava-stub|low
tier\\lite=local:stub|llava-stub|low
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
printf '{"version":1,"endpoints":[{"id":"local:stub","label":"Stub","base_url":"http://127.0.0.1:%s/v1","model":"llava-stub","server":"openai-compatible","context_window":131072}]}\n' "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

launch() {
    (cd "$work" && exec "$build/relay" $1 --engine-core libvterm) >>"$out/relay.log" 2>&1 & relay_pid=$!
    sleep 8
    win= best=0
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$((WIDTH * HEIGHT)); win=$w; }
    done
    [[ -n $win ]] || { echo 'Relay window did not open' >&2; cat "$out/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"
    xdotool windowfocus "$win"
    sleep 2
}
type_text() { xdotool type --delay 12 -- "$1"; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 1; import -window "$win" "$out/$1.png"; }
command() { type_text "$1"; xdotool key Return; sleep 8; }

: > "$out/relay.log"
launch "--workspace $work --fresh"
shot 01-sound-table
xdotool mousemove 150 188 click 1; sleep 1
table_win=$(xdotool search --name '^numbers.csv$' | tail -1)
[[ -n $table_win ]] || { echo 'sortable table did not open' >&2; exit 1; }
import -window root "$out/07-table-open.png"
xdotool windowclose "$table_win"
sleep 1
xdotool windowfocus "$win"
command 'clear; relay-show chart.html; relay-show clip.mp4'
shot 02-chart-video
type_text 'Explain the sound, equation and table'; xdotool key ctrl+Return; sleep 12
shot 03-agent-math
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null || true; sleep 1
if ! rg -a -q 'relay-media:' "$XDG_DATA_HOME/relay/state/scrollback"; then
    echo 'saved pane lost its media links' >&2
    exit 1
fi
# #15G5: the agent's Markdown table is drawn and wrapped inside its cells, with no
# "open sortable table" row; only relay-show's CSV has a table manifest.
python3 - "$XDG_DATA_HOME/relay/state/scrollback" "$XDG_CACHE_HOME/relay/media" <<'PY'
import pathlib, re, sys
scrollback, media = map(pathlib.Path, sys.argv[1:])
names = [p.name for p in media.glob('*.json') if '"Markdown table"' in p.read_text(errors='replace')]
if names:
    raise SystemExit(f'Markdown table still wrote a sortable-table manifest: {names}')
escape = re.compile(r'\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b\[[0-9;?]*[A-Za-z]')
lines = [escape.sub('', line) for p in scrollback.glob('*.txt')
         for line in p.read_text(errors='replace').splitlines()]
note = [i for i, line in enumerate(lines) if line.startswith('Large') and '│' in line]
if not note:
    raise SystemExit('agent table row "Large" not found in saved scrollback')
row = note[-1]
bars = [m.start() for m in re.finditer('│', lines[row])]
more = lines[row + 1] if row + 1 < len(lines) else ''
if not re.match(r' +│ +│ \S', more) or [m.start() for m in re.finditer('│', more)] != bars:
    raise SystemExit(f'wide cell did not wrap inside its column:\n{lines[row]}\n{more}')
PY
launch ''
shot 04-restored
command 'clear; relay-show demo.svg; relay-show demo.pdf; relay-show animate.gif'
shot 05-svg-pdf-animation
animated=0
for _ in 1 2 3 4 5 6 7 8; do
    sleep 0.12
    import -window "$win" "$out/06-animation-frame.png"
    if python3 - "$out/05-svg-pdf-animation.png" "$out/06-animation-frame.png" <<'PY'
from PIL import Image
import sys
one, two = (Image.open(path).convert('RGB') for path in sys.argv[1:])
# The sample is inside the GIF's coloured field; a changed colour proves it advanced.
raise SystemExit(0 if one.getpixel((50, 580)) != two.getpixel((50, 580)) else 1)
PY
    then animated=1; break; fi
done
[[ $animated == 1 ]] || { echo 'animated GIF did not advance between screenshots' >&2; exit 1; }
command 'clear; relay-show animate.webp'
shot 08-webp
printf 'done: %s\n' "$out"
