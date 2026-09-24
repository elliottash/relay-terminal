#!/usr/bin/env bash
# Stage card #MTCS's situation: a pane whose saved text contains Relay's own word-wrapped
# output, restored, so the person can resize the pane and judge whether the wrap follows.
#
# What it does: a disposable HOME (your display is used, your Relay state is not), one
# short-lived Relay run to write a real layout + scrollback pair, the scrollback swapped for
# one that carries two prose blocks (rows hard-wrapped at 45 columns plus the trailer with
# the logical lines, exactly the format windowstate::writeScrollback writes), and a second
# run that reopens the saved layout. The restored pane replays the rows and re-registers the
# blocks, so the paragraph is laid out to the pane's width and re-wraps on resize.
#
#   docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/stage.sh
# Stop it with: kill $(cat /tmp/rl-mtcs.last.pid)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bin=${RELAY_BIN:-"$(cd "$here/../../.." && pwd)/build/relay"}
[ -x "$bin" ] || { echo "no relay binary at $bin (build it, or set RELAY_BIN)" >&2; exit 1; }

sandbox=$(mktemp -d /tmp/rl-mtcs.XXXX)
echo "$sandbox" > /tmp/rl-mtcs.last.sandbox
cleanup() { for pid in ${relay2_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null || true; done; }
trap cleanup EXIT

mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp" "$sandbox/work"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus" 2>/dev/null || true
export HOME="$sandbox/home" XDG_CONFIG_HOME="$sandbox/home/.config" \
       XDG_DATA_HOME="$sandbox/home/.local/share" XDG_CACHE_HOME="$sandbox/home/.cache" \
       XDG_RUNTIME_DIR="$sandbox/run" TMPDIR="$sandbox/tmp" RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # never hand these windows to the caller's running Relay
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
    > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

own_xvfb=
if [ -z "${DISPLAY:-}" ]; then
    # No display here (agent context): use a virtual one so the run still happens; the
    # person running this from a desktop session gets their own display instead.
    for n in $(seq 170 199); do [ -e /tmp/.X11-unix/X$n ] || { display=:$n; break; }; done
    Xvfb "$display" -screen 0 1000x640x24 >/dev/null 2>&1 & xvfb_pid=$!
    sleep 2; export DISPLAY="$display"; own_xvfb=1
fi

# Phase 1: one window, one shell pane; quit on SIGTERM so the layout and scrollback are saved.
"$bin" --workspace "$sandbox/work" --clean-shell --fresh > "$sandbox/phase1.log" 2>&1 &
phase1_pid=$!
sleep 14
kill -TERM "$phase1_pid" 2>/dev/null || true
wait "$phase1_pid" 2>/dev/null || true

saved=
for _ in $(seq 1 10); do
    saved=$(find "$sandbox" -path '*scrollback/*.txt' -type f 2>/dev/null | head -1)
    [ -n "$saved" ] && break
    sleep 1
done
[ -n "$saved" ] || { echo "no scrollback was saved; see $sandbox/phase1.log" >&2; exit 1; }

cp "$here/scrollback-with-prose.txt" "$saved"
echo "staged pane text in $saved"

# Phase 2: reopen the saved layout. The pane replays the rows at the next shell prompt and
# re-registers the prose blocks, so the paragraph re-wraps to the pane's width.
"$bin" --clean-shell > "$sandbox/phase2.log" 2>&1 &
relay2_pid=$!
echo "$relay2_pid" > /tmp/rl-mtcs.last.pid
sleep 6
echo "relay is running (pid $relay2_pid, sandbox $sandbox)"
if [ -n "$own_xvfb" ]; then
    import -window root "$here/staged-restored.png" 2>/dev/null || true
    echo "headless run: screenshot at $here/staged-restored.png (log: $sandbox/phase2.log)"
fi
# Leave it running for the person; the EXIT trap cleans up if this script is killed.
relay2_pid=
