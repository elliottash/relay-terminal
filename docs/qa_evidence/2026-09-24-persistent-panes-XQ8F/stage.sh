#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
out=$(cd "$(dirname "$0")" && pwd)
binary=${RELAY_QA_BINARY:-$repo/build/relay}
profile=$(mktemp -d -t relay-xq8f-profile-XXXXXX)
mkdir -p "$profile/config" "$profile/data" "$profile/cache" "$profile/runtime" "$profile/workspace"
chmod 700 "$profile/runtime"
export XDG_CONFIG_HOME="$profile/config" XDG_DATA_HOME="$profile/data"
export XDG_CACHE_HOME="$profile/cache" XDG_RUNTIME_DIR="$profile/runtime"
export RELAY_DATA_DIR="$repo"
export QT_QPA_PLATFORM=xcb
tmux -L relay list-sessions -F '#{session_name}' 2>/dev/null | sort > "$profile/before" || true
sessions=()
app=
cleanup() {
    if [[ -n "$app" ]]; then kill -TERM "$app" 2>/dev/null || true; wait "$app" 2>/dev/null || true; fi
    for session in "${sessions[@]}"; do tmux -L relay kill-session -t "$session" 2>/dev/null || true; done
}
trap cleanup EXIT

new_sessions() {
    tmux -L relay list-sessions -F '#{session_name}' 2>/dev/null | sort > "$profile/after" || true
    comm -13 "$profile/before" "$profile/after"
}

wait_window() {
    local i
    for i in $(seq 1 50); do
        window=$(xdotool search --onlyvisible --name '^Relay' 2>/dev/null | head -1 || true)
        if [[ -n "$window" ]]; then return 0; fi
        sleep 0.2
    done
    return 1
}

"$binary" --workspace "$profile/workspace" --fresh > "$profile/gui-first.log" 2>&1 &
app=$!
wait_window
xdotool windowfocus --sync "$window"
sleep 2
xdotool mousemove 1230 57 click 1
xdotool type --clearmodifiers --delay 20 'ssh localhost'
xdotool key Return
sleep 5
first=$(new_sessions | head -1)
[[ -n "$first" ]]
sessions+=("$first")
tmux -L relay send-keys -t "$first" 'cd /tmp' Enter
sleep 2
[[ $(tmux -L relay list-panes -t "$first" -F '#{pane_current_path}') == /tmp ]]
tmux -L relay send-keys -t "$first" top Enter
sleep 2
[[ $(tmux -L relay list-panes -t "$first" -F '#{pane_current_command}') == top ]]
import -window root "$out/01-remote-login.png"
xdotool key ctrl+e
sleep 9
second=$(new_sessions | grep -v -F "$first" | head -1)
[[ -n "$second" ]]
sessions+=("$second")
[[ $(tmux -L relay list-panes -t "$second" -F '#{pane_current_path}') == /tmp ]]
import -window root "$out/02-remote-split.png"
kill -TERM "$app"
wait "$app" || true
app=
[[ $(tmux -L relay list-panes -t "$first" -F '#{pane_current_command}') == top ]]
sleep 1

"$binary" > "$profile/gui-restart.log" 2>&1 &
app=$!
wait_window
xdotool windowfocus --sync "$window"
sleep 12
[[ $(tmux -L relay list-panes -t "$first" -F '#{pane_current_command}') == top ]]
import -window root "$out/03-restarted.png"
kill -TERM "$app"
wait "$app" || true
app=

printf 'profile=%s\nbinary=%s\nfirst=%s\nsecond=%s\nfirst-cwd=/tmp\nsecond-cwd=/tmp\nprogram-after-restart=top\n' \
    "$profile" "$binary" "$first" "$second" > "$out/run.txt"
