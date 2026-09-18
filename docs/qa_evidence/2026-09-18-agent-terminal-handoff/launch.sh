#!/usr/bin/env bash
# launch.sh <display> <port> [handoff-setting]: private X server, isolated profile, stub provider, Relay.
set -u
QA=$(cd "$(dirname "$0")" && pwd); display=$1; port=$2; setting=${3:-}
rm -rf "$QA/home" "$QA/work"; mkdir -p "$QA/home"/{config/RelayTerminal,data,state,cache,run,tmp} "$QA/work" "$QA/shots"
chmod 700 "$QA/home/run"
cat > "$QA/work/ask.sh" <<'ASK'
#!/usr/bin/env bash
# Needs a real terminal: refuses without one, then asks a question.
test -t 0 || { echo "no tty"; exit 9; }
read -p "Continue? " answer
echo "got: $answer"
[[ $answer == y ]]
ASK
chmod +x "$QA/work/ask.sh"
{
cat <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[hints]
enabled=false
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF
[[ -n $setting ]] && printf '[agent]\nterminal_handoff=%s\n' "$setting"
} > "$QA/home/config/RelayTerminal/relay.conf"
: > "$QA/requests.log"
python3 "$QA/stub-provider.py" "$port" "$QA/requests.log" & echo $! > "$QA/stub.pid"
Xvfb "$display" -screen 0 1400x800x24 >/dev/null 2>&1 & echo $! > "$QA/xvfb.pid"
sleep 2
env DISPLAY="$display" XDG_CONFIG_HOME="$QA/home/config" XDG_DATA_HOME="$QA/home/data" \
    XDG_STATE_HOME="$QA/home/state" XDG_CACHE_HOME="$QA/home/cache" XDG_RUNTIME_DIR="$QA/home/run" \
    TMPDIR="$QA/home/tmp" RELAY_KEYRING=off RELAY_CUSTOM_API_KEY=loopback-stub-not-a-key QT_QPA_PLATFORM=xcb \
    "${RELAY:-build/relay}" --clean-shell --fresh --workspace "$QA/work" \
    > "$QA/relay-stderr.log" 2>&1 & echo $! > "$QA/relay.pid"
sleep 7
win=$(DISPLAY="$display" xdotool search --pid "$(cat "$QA/relay.pid")" --name '^Relay ' | head -1)
[[ -z $win ]] && { echo "no Relay window"; tail -5 "$QA/relay-stderr.log"; exit 1; }
DISPLAY="$display" xdotool windowmove "$win" 0 0 windowsize "$win" 1400 800 windowfocus "$win"
echo "$win" > "$QA/win"; echo "window $win"
