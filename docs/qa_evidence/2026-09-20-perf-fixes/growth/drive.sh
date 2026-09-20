#!/usr/bin/env bash
# #PF4K transcript area: boot Relay under Xvfb with an isolated profile pointed at stub.py, send
# one or more prompts, and sample per-thread CPU and memory while they stream.
#
#   ./drive.sh <label> <seconds> <prompt>...        one run, one sample file
#
# Environment:
#   RELAY_BIN      the binary to run (default: /tmp/claude-1000/pf4k/build/relay)
#   OUT            where samples/logs go (default: <script dir>/out)
#   KEEP=1         leave the sandbox and the app up (for a follow-up probe); prints the pids
#   PRE_SLEEP      seconds to wait after the window appears before the first prompt
#   GAP            seconds between prompts (default 2)
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
label=$1; secs=$2; shift 2
bin=${RELAY_BIN:-/tmp/claude-1000/pf4k/build/relay}
out=${OUT:-$here/out}; mkdir -p "$out"
gap=${GAP:-2}; pre=${PRE_SLEEP:-1}
width=1400 height=900
port=${PORT:-8931}

display=
for n in $(seq 210 250); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sb=${REUSE_SB:-$here/sb.$$}
stub_pid= xvfb_pid= relay_pid= sampler_pid=
cleanup() {
    local pid
    for pid in $sampler_pid $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    sleep 0.5
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill -9 "$pid" 2>/dev/null; done
    [[ ${KEEP:-0} = 1 ]] || rm -rf "$sb"
}
[[ ${KEEP:-0} = 1 ]] || trap cleanup EXIT

python3 "$here/stub.py" "$port" "$here/big.txt" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sb/h" "$sb/r" "$sb/t"; chmod 700 "$sb/r"
ln -sfn "/run/user/$(id -u)/bus" "$sb/r/bus"
export HOME=$sb/h XDG_RUNTIME_DIR=$sb/r TMPDIR=$sb/t
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_STATE_HOME=$HOME/.local/state
work=$HOME/p
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
[[ -n ${REUSE_SB:-} ]] || cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
base=http://127.0.0.1:__PORT__/v1
model=stub
extra={}
[security]
approvals_chosen=true
CONF
sed -i "s|__PORT__|$port|" "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
[[ -n ${EXTRA_CONF:-} ]] && printf '%s\n' "$EXTRA_CONF" >>"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 1000000}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
[[ -n ${SEED_SESSIONS:-} ]] && cp -r "$SEED_SESSIONS/." "$XDG_DATA_HOME/" 2>/dev/null
# The pane comes back from a seeded window layout with input_mode=agent, so nothing a run types
# can be routed to the shell and no key-press dance is needed (src/WindowState.h).
mkdir -p "$XDG_DATA_HOME/relay/state"
if [[ -z ${REUSE_SB:-} ]]; then
python3 - "$XDG_DATA_HOME/relay/state/windows.json" "$work" <<'PY'
import json, sys, time, uuid
path, work = sys.argv[1], sys.argv[2]
pane = {"cwd": work, "workspace": work, "agent_role": "main", "agent_mode": "",
        "input_mode": "agent", "effort": "", "preset": "local:stub", "model": "stub",
        "scrollback": uuid.uuid4().hex}
doc = {"version": 1, "saved": int(time.time()),
       "windows": [{"tabs": [{"pane": pane}], "current": 0, "geometry": [0, 0, 1400, 900]}]}
open(path, "w").write(json.dumps(doc))
PY
chmod 600 "$XDG_DATA_HOME/relay/state/windows.json"
fi

if [[ ${IPC:-0} = 1 ]]; then
    mkdir -p "$sb/bin"
    cat >"$sb/bin/python3" <<'SHIM'
#!/bin/bash
# #PF4K: copy the worker <-> GUI channel (newline JSON, protocol docs/AGENT-SESSIONS-PROTOCOL.md).
for a in "$@"; do
    case "$a" in
        *backend/worker.py)
            tee -a "$RELAY_IPC_DIR/gui2worker.jsonl" | /usr/bin/python3 "$@" | tee -a "$RELAY_IPC_DIR/worker2gui.jsonl"
            exit $?;;
    esac
done
exec /usr/bin/python3 "$@"
SHIM
    chmod +x "$sb/bin/python3"
    export PATH=$sb/bin:$PATH RELAY_IPC_DIR=$out/$label.ipc
    mkdir -p "$RELAY_IPC_DIR"
fi
launch_ns=$(date +%s%N)
(cd "$work" && exec "$bin" --clean-shell) >"$out/$label.relay.log" 2>&1 &
relay_pid=$!
for i in $(seq 1 40); do
    win=$(xdotool search --pid "$relay_pid" 2>/dev/null | tail -1)
    [[ -n $win ]] && break
    sleep 0.5
done
[[ -z ${win:-} ]] && { echo "no window"; tail -20 "$out/$label.relay.log"; exit 1; }
echo "window_ms=$(( ($(date +%s%N) - launch_ns) / 1000000 )) sandbox=$sb"
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowactivate "$win" 2>/dev/null; xdotool windowfocus "$win"
sleep "${SETTLE:-8}"
# auto -> terminal -> agent: nothing typed here may be routed to the shell.  Ctrl+I is ignored
# for the first seconds after the window maps, so the chip is read back and the pair repeated.
chipmode() {
    import -window "$win" "$sb/chip.png" 2>/dev/null
    convert "$sb/chip.png" -crop 200x40+1200+805 +repage -scale 400% "$sb/chip2.png" 2>/dev/null
    tesseract "$sb/chip2.png" - --psm 7 2>/dev/null | tr -d ' \n'
}
xdotool windowfocus "$win"; sleep 0.3
xdotool type --delay 20 -- 'xx'; sleep 0.4
xdotool key --clearmodifiers ctrl+a; sleep 0.2; xdotool key BackSpace; sleep 0.5
mode=$(chipmode)
echo "input_mode=$mode"
[[ $mode = *agent* ]] || { echo "not in agent mode: $mode"; exit 1; }
sleep "$pre"

# ---- sampler: main (GUI) thread and whole-process CPU, RSS, worker and stub CPU ----------------
ticks=$(getconf CLK_TCK)
sample() {
    local f=$out/$label.samples.tsv
    printf 'ms\tgui_utime\tgui_stime\tproc_utime\tproc_stime\tthreads\trss_kb\tworker_cpu\tvoluntary_cs\tnonvoluntary_cs\n' >"$f"
    local t0 now gui proc wpid
    t0=$(date +%s%N)
    while kill -0 "$relay_pid" 2>/dev/null; do
        now=$(( ($(date +%s%N) - t0) / 1000000 ))
        gui=$(awk '{print $14"\t"$15}' "/proc/$relay_pid/task/$relay_pid/stat" 2>/dev/null) || break
        proc=$(awk '{print $14"\t"$15"\t"$20}' "/proc/$relay_pid/stat" 2>/dev/null) || break
        rss=$(awk '/^VmRSS/{print $2}' "/proc/$relay_pid/status" 2>/dev/null)
        vcs=$(awk '/^voluntary_ctxt/{print $2}' "/proc/$relay_pid/task/$relay_pid/status" 2>/dev/null)
        ncs=$(awk '/^nonvoluntary_ctxt/{print $2}' "/proc/$relay_pid/task/$relay_pid/status" 2>/dev/null)
        wpid=$(pgrep -P "$relay_pid" -f 'worker.py' | head -1)
        wcpu=0
        [[ -n $wpid ]] && wcpu=$(awk '{print $14+$15}' "/proc/$wpid/stat" 2>/dev/null)
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$now" "$gui" "$proc" "${rss:-0}" "${wcpu:-0}" "${vcs:-0}" "${ncs:-0}" >>"$f"
        sleep 0.2
    done
}
sample &
sampler_pid=$!

t() { xdotool type --delay ${TYPE_DELAY:-12} -- "$1"; sleep ${TYPE_PAUSE:-0.6}; xdotool key --clearmodifiers Return; }
# perf / bpftrace probes fire once, PERF_AFTER seconds after the first prompt goes out.
probe() {
    sleep "${PERF_AFTER:-3}"
    if [[ -n ${PERF_SECS:-} ]]; then
        sudo -n perf record -g --call-graph fp -F ${PERF_FREQ:-499} -p "$relay_pid" \
            -o "$out/$label.perf.data" -- sleep "$PERF_SECS" >/dev/null 2>&1
        sudo -n chown "$(id -u):$(id -g)" "$out/$label.perf.data" 2>/dev/null
    fi
    # #PPR4 growth: a second window later in the same conversation, so an early profile and a
    # late one come from one run and can be subtracted.
    if [[ -n ${PERF2_SECS:-} ]]; then
        sleep "${PERF2_AFTER:-30}"
        sudo -n perf record -g --call-graph fp -F ${PERF_FREQ:-499} -p "$relay_pid" \
            -o "$out/$label.perf2.data" -- sleep "$PERF2_SECS" >/dev/null 2>&1
        sudo -n chown "$(id -u):$(id -g)" "$out/$label.perf2.data" 2>/dev/null
    fi
    if [[ -n ${SYSCALL_SECS:-} ]]; then
        sudo -n timeout "${SYSCALL_SECS}s" bpftrace -e "
          tracepoint:raw_syscalls:sys_enter /tid == $relay_pid/ { @[args->id] = count(); }
          " >"$out/$label.syscalls.txt" 2>&1
    fi
    if [[ -n ${BPF_SECS:-} ]]; then
        sudo -n timeout "${BPF_SECS}s" bpftrace -e "
          tracepoint:syscalls:sys_exit_ppoll  /tid == $relay_pid/ { @back[tid] = nsecs; }
          tracepoint:syscalls:sys_enter_ppoll /tid == $relay_pid && @back[tid] > 0/ {
              @busy_us = hist((nsecs - @back[tid]) / 1000); @n = count();
              if (nsecs - @back[tid] > 50000000) { printf(\"stall %d us\\n\", (nsecs - @back[tid]) / 1000); }
          }
          END { clear(@back); }" >"$out/$label.ppoll.txt" 2>&1
    fi
}
[[ -n ${PERF_SECS:-}${PERF2_SECS:-}${BPF_SECS:-}${SYSCALL_SECS:-} ]] && { probe & probe_pid=$!; }
# #PPR4 item B: keys to press before the first prompt (Alt+Shift+R opens the Activity pane).
if [[ -n ${PRE_KEYS:-} ]]; then
    for k in $PRE_KEYS; do xdotool key --clearmodifiers "$k"; sleep 1.5; done
    import -window root "$out/$label.prekeys.png" 2>/dev/null
fi
for p in "$@"; do
    echo "# prompt at $(date +%s%N)" >>"$out/$label.marks"
    t "$p"
    sleep "$gap"
done
# #PPR4 item B: keys pressed once the first prompt is away (Alt+Shift+R opens the Activity pane
# on the pane that is streaming, which is the path Pane::attachInternals is written for).
if [[ -n ${POST_KEYS:-} ]]; then
    sleep "${POST_AFTER:-2}"
    for k in $POST_KEYS; do xdotool key --clearmodifiers "$k"; sleep 1; done
    sleep 2
    import -window root "$out/$label.postkeys.png" 2>/dev/null
fi
sleep "$secs"
[[ -n ${probe_pid:-} ]] && wait "$probe_pid" 2>/dev/null
kill "$sampler_pid" 2>/dev/null
kill "$sampler_pid" 2>/dev/null
import -window root "$out/$label.png" 2>/dev/null
cp -r "$sb/h/.local/share/relay/logs" "$out/$label.logs" 2>/dev/null
grep -c . "$out/$label.samples.tsv" >/dev/null
echo "clk_tck=$ticks relay_pid=$relay_pid display=$display load=$(cut -d' ' -f1-3 /proc/loadavg)"
[[ ${KEEP:-0} = 1 ]] && { echo "KEEP: sandbox $sb pids relay=$relay_pid stub=$stub_pid xvfb=$xvfb_pid win=$win display=$display"; exit 0; }
