#!/usr/bin/env bash
# #HEY7 live check: a shell pane prints 30,000 coloured lines (more than the terminal's 10,000-row
# ring), Relay quits, and Relay comes back. The rows the ring let go of must be in the pane's text
# journal, the rest in the tail file, and the restored pane must show the "earlier lines" row.
#
#   RELAY_BIN=<relay> ./stage.sh <out dir>
set -uo pipefail
bin=${RELAY_BIN:?set RELAY_BIN}
out=${1:?out dir}
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$out"; width=1400 height=900
display=; for n in $(seq 340 399); do [[ -e /tmp/.X11-unix/X$n || -e /tmp/.X$n-lock ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-hey7.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null || true; done; sleep 2; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
kill -0 "$xvfb_pid" || { echo "Xvfb did not start on $display" >&2; exit 1; }
echo "display $display" > "$out/display.txt"
export DISPLAY=$display RELAY_KEYRING=off RELAY_DATA_DIR=$repo
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp" "$sandbox/work"; chmod 700 "$sandbox/run"
# The user bus, so panes run as they do on the owner's machine: under systemd memory isolation,
# with the engine holding the scrollback (without it the pane shell runs in the tmux holder).
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config \
       XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache \
       TMUX_TMPDIR=$sandbox/tmp   # the pane shells run in tmux -L relay: a private server, not the user's
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
# The first shell prints the lines once; the restored one does not.
cat > "$HOME/.bashrc" <<'EOF'
if [ ! -f "$HOME/.printed" ]; then
  touch "$HOME/.printed"
  for i in $(seq 1 30000); do printf '\033[1;3%dmline %05d\033[0m plain tail\n' $((i % 7 + 1)) "$i"; done
fi
EOF
shot() { import -window root "$out/$1.png"; }

(cd "$sandbox/work" && exec "$bin" --workspace "$sandbox/work") > "$out/relay-1.log" 2>&1 & relay_pid=$!
sleep 25
shot 01-after-30000-lines
text=$XDG_DATA_HOME/relay/text
{
  echo "journals after the first run (live pane, before quit):"
  find "$text" -type f -printf '%P %s\n' | sort
} > "$out/files-1.txt"
kill -TERM "$relay_pid"; wait "$relay_pid" || true; relay_pid=
{
  echo "after quit:"
  find "$text" "$XDG_DATA_HOME/relay/state/scrollback" -type f -printf '%P %s\n' | sort
  for d in "$text"/*/; do
    echo "journal $(basename "$d"): meta $(cat "$d/meta.json")"
    python3 -S "$repo/backend/relay_core/textjournal.py" cat "$d" --plain | sed -n '1p;$p'
    echo "lines: $(python3 -S "$repo/backend/relay_core/textjournal.py" cat "$d" --plain | wc -l)"
  done
  for f in "$XDG_DATA_HOME"/relay/state/scrollback/*.txt; do
    echo "tail $(basename "$f"): $(grep -c 'line [0-9]' "$f") numbered rows, first: $(grep -m1 -o 'line [0-9]*' "$f"), last: $(grep -o 'line [0-9]*' "$f" | tail -1)"
  done
} > "$out/files-2.txt"

# No --workspace: an explicit workspace opens a fresh window instead of the saved layout.
(cd "$sandbox/work" && exec "$bin") > "$out/relay-2.log" 2>&1 & relay_pid=$!
sleep 20
shot 02-restored-top
# Scroll the restored pane to its oldest row, where the earlier-lines row is.
"$repo/scripts/relay-drive" action terminal.scrollTop > "$out/scroll-top.json" 2>&1 || true
sleep 2
shot 03-restored-scrolled-top
kill -TERM "$relay_pid"; wait "$relay_pid" || true; relay_pid=
{
  echo "after the second quit (the restored pane's journal goes on in the same directory):"
  find "$text" "$XDG_DATA_HOME/relay/state/scrollback" -type f -printf '%P %s\n' | sort
  for d in "$text"/*/; do
    echo "journal $(basename "$d"): $(python3 -S "$repo/backend/relay_core/textjournal.py" cat "$d" --plain | grep -c 'line [0-9]') numbered lines"
  done
  for f in "$XDG_DATA_HOME"/relay/state/scrollback/*.txt; do
    echo "tail $(basename "$f"): first: $(grep -m1 -o 'line [0-9]*' "$f"), last: $(grep -o 'line [0-9]*' "$f" | tail -1)"
  done
  # Everything the pane ever printed, once: journal + tail, numbered lines, no gaps or repeats.
  d=$(ls -d "$text"/*/ | head -1)
  { python3 -S "$repo/backend/relay_core/textjournal.py" cat "$d" --plain; cat "$XDG_DATA_HOME"/relay/state/scrollback/*.txt; } \
    | grep -o 'line [0-9]*' | awk '{print $2+0}' > "$sandbox/all.txt"
  echo "numbered lines across journal + tail: $(wc -l < "$sandbox/all.txt"), distinct: $(sort -u "$sandbox/all.txt" | wc -l), min $(sort -n "$sandbox/all.txt" | head -1), max $(sort -n "$sandbox/all.txt" | tail -1)"
} > "$out/files-3.txt"
# The journal the restore row links to, read as the click reads it (the first screen of less).
python3 -S "$repo/backend/relay_core/textjournal.py" cat "$(ls -d "$text"/*/ | head -1)" | head -40 > "$out/cat-head.ansi"
