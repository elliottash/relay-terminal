#!/usr/bin/env bash
# #PF4K sphinxpad re-measure, terminal area (#6W0Z): cat 50 MB, seq, GUI statx, paints, scrollback.
#   term.sh <tag> <relay binary> <data root>
# Drives the pane's SHELL only: input mode is forced to shell in relay.conf and every line is
# submitted with ctrl+shift+Return, which never reaches an agent. A warm-up line must land in a
# stamp file or the run aborts without typing anything else (the profiler's guard).
set -uo pipefail
TAG=$1; BIN=$2; ROOT=$3
P=/tmp/rx/t
IN=$HOME/relay-perf/engine/in
export DISPLAY=${RDISP:-:271}
rm -rf $P/h; mkdir -p $P/h/{run,cfg,data,state,cache,tmp} $P/work $P/out
chmod 700 $P/h/run
ln -sfn "/run/user/$(id -u)/bus" "$P/h/run/bus"
export XDG_RUNTIME_DIR=$P/h/run XDG_CONFIG_HOME=$P/h/cfg XDG_DATA_HOME=$P/h/data \
       XDG_STATE_HOME=$P/h/state XDG_CACHE_HOME=$P/h/cache TMPDIR=$P/h/tmp
export RELAY_KEYRING=off QT_QPA_PLATFORM=xcb RELAY_DATA_DIR=$ROOT RELAY_NO_URL_HANDLER=1
# belt and braces: no request can leave this box even if something were to reach a provider
export https_proxy=http://127.0.0.1:9 http_proxy=http://127.0.0.1:9 no_proxy=127.0.0.1,localhost
mkdir -p "$P/h/cfg/RelayTerminal"
cat > "$P/h/cfg/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[security]
approvals_chosen=true
[input]
default=shell
CONF
hz=$(getconf CLK_TCK)

(setsid "$BIN" --clean-shell --fresh -w $P/work > $P/relay-$TAG.log 2>&1 &)
sleep 14
PID=""; WIN=""
for w in $(xdotool search --name "Relay"); do
  n=$(xdotool getwindowname "$w" 2>/dev/null); p=$(xdotool getwindowpid "$w" 2>/dev/null)
  case "$n" in Relay*) [ -n "$p" ] && WIN=$w && PID=$p;; esac
done
[ -z "$PID" ] && { echo "$TAG NO_WINDOW"; exit 1; }
xdotool windowfocus --sync "$WIN" 2>/dev/null
echo "$TAG pid=$PID win=$WIN load=$(cut -d' ' -f1 /proc/loadavg)"

# ---- warm-up guard: prove the shell is taking our lines before anything else is typed ----------
W=$P/out/warm.stamp; rm -f "$W"
xdotool type --delay 20 -- "echo warm > $W"
xdotool key --clearmodifiers ctrl+shift+Return
for _ in $(seq 1 60); do [ -s "$W" ] && break; sleep 0.25; done
[ -s "$W" ] || { echo "$TAG WARMUP_FAILED - aborting"; kill -TERM $PID; exit 1; }
echo "$TAG warmup ok"

snap() { for t in /proc/$PID/task/*; do [ -r "$t/stat" ] || continue
  awk -v tid="${t##*/}" '{n=split($0,a," "); print tid, a[14]+a[15]}' "$t/stat"; done; }

run() {   # run <name> <shell command>
  local name=$1 cmd=$2
  local stamp=$P/out/$name.stamp
  rm -f "$stamp"
  declare -A before; while read -r tid ticks; do before[$tid]=$ticks; done < <(snap)
  xdotool type --delay 4 -- "s=\$(date +%s%N); $cmd; e=\$(date +%s%N); echo \$s \$e > $stamp"
  xdotool key --clearmodifiers ctrl+shift+Return
  local deadline=$((SECONDS+240))
  while [ ! -s "$stamp" ] && [ $SECONDS -lt $deadline ]; do sleep 0.1; done
  sleep 1.5
  local line="$TAG $name"
  if [ -s "$stamp" ]; then read -r s e <"$stamp"
    line="$line wall_s=$(awk -v s=$s -v e=$e 'BEGIN{printf "%.3f",(e-s)/1e9}')"
  else line="$line wall_s=TIMEOUT"; fi
  declare -A after; while read -r tid ticks; do after[$tid]=$ticks; done < <(snap)
  for tid in "${!after[@]}"; do
    d=$(( ${after[$tid]} - ${before[$tid]:-0} )); [ "$d" -gt 0 ] || continue
    c=$(cat /proc/$PID/task/$tid/comm 2>/dev/null)
    [ "$tid" = "$PID" ] && c="GUI"; [ "$c" = "relay" ] && c="pty"
    line="$line ${c}_cpu_s=$(awk -v d=$d -v h=$hz 'BEGIN{printf "%.2f",d/h}')"
  done
  echo "$line rss_kb=$(awk '/VmRSS/{print $2}' /proc/$PID/status) load=$(cut -d' ' -f1 /proc/loadavg)"
}

pss() { awk '/^Pss:/{s+=$2} END{print s}' /proc/$PID/smaps_rollup 2>/dev/null; }

run geom 'echo grid $(tput cols)x$(tput lines)'
for i in 1 2 3; do run "cat50_$i" "cat $IN/b64_50.txt"; done

# ---- statx a second on each thread while 50 MB goes through -----------------------------------
( sleep 1.2; sudo -n perf stat --per-thread -e syscalls:sys_enter_statx -p $PID -- sleep 4 \
    2>&1 | grep -E "statx|seconds time" | sed "s/^/$TAG statx /" ) &
STATP=$!
run "cat50_statx" "cat $IN/b64_50.txt $IN/b64_50.txt"
wait $STATP 2>/dev/null

# ---- paints a second, by uprobe on TerminalView::paintEvent / paintRow -------------------------
A=$(nm -C "$BIN" 2>/dev/null | grep -E "T relay::TerminalView::paintEvent" | awk '{print $1}')
B=$(nm -C "$BIN" 2>/dev/null | grep -E "T relay::TerminalView::paintRow" | awk '{print $1}')
if [ -n "$A" ]; then
  sudo -n perf probe -d "*p_$TAG" >/dev/null 2>&1; sudo -n perf probe -d "*r_$TAG" >/dev/null 2>&1
  sudo -n perf probe -x "$BIN" --add "p_$TAG=0x$A" >/dev/null 2>&1
  [ -n "$B" ] && sudo -n perf probe -x "$BIN" --add "r_$TAG=0x$B" >/dev/null 2>&1
  PR=$(sudo -n perf probe -l 2>&1 | grep -oE "probe_[a-z0-9_]*:p_$TAG" | head -1)
  RR=$(sudo -n perf probe -l 2>&1 | grep -oE "probe_[a-z0-9_]*:r_$TAG" | head -1)
  if [ -n "$PR" ]; then
    ( sleep 1.2; sudo -n perf stat -e "$PR${RR:+,$RR}" -p $PID -- sleep 4 2>&1 \
        | grep -E "p_$TAG|r_$TAG|seconds time" | sed "s/^/$TAG paints /" ) &
    PP=$!
    run "cat50_paint" "cat $IN/b64_50.txt $IN/b64_50.txt"
    wait $PP 2>/dev/null
    sudo -n perf probe -d "*p_$TAG" >/dev/null 2>&1; sudo -n perf probe -d "*r_$TAG" >/dev/null 2>&1
  else echo "$TAG paints NO_PROBE"; fi
else echo "$TAG paints NO_SYMBOL"; fi

# ---- seq, and what a scrollback line costs -----------------------------------------------------
p0=$(pss)
run "seq_1" "seq 1 2000000"
p1=$(pss)
echo "$TAG scrollback pss_before_kb=$p0 pss_after_kb=$p1 delta_kb=$((p1-p0))"
for i in 2 3; do run "seq_$i" "seq 1 2000000"; done
echo "$TAG final rss_kb=$(awk '/VmRSS/{print $2}' /proc/$PID/status) pss_kb=$(pss)"
kill -TERM $PID 2>/dev/null; sleep 3
