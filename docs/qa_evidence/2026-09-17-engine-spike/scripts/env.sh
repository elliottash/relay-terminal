export DISPLAY=:77
BIN=/tmp/claude-1000/spike-build/engine/relay-vterm-spike
EV=/home/elliott/repos/relay-terminal/.claude/worktrees/agent-a227baa7bfb4aab80/docs/qa_evidence/2026-09-17-engine-spike
mkdir -p "$EV"
shot() { import -window root "$EV/$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
startspike() {
  pkill -f relay-vterm-spike; sleep 0.3
  "$BIN" --cwd /tmp/claude-1000/sp/work --dump /tmp/claude-1000/sp/dump.txt "$@" >/tmp/claude-1000/sp/spike.log 2>&1 &
  SPID=$!
  sleep 1.5
  W=$(xdotool search --pid $SPID | tail -1)
  xdotool windowmove $W 0 0; xdotool windowfocus $W; xdotool mousemove 300 200
  sleep 0.3
}
