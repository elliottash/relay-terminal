#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Smoke-test an installed Relay (any distribution). Needs xvfb-run, ps (procps) and xwininfo.
set -euo pipefail

step() { printf '\n== %s\n' "$*"; }

step "installed files"
for f in /usr/bin/relay /usr/share/relay/backend/worker.py /usr/share/relay/shell/integration.bash \
    /usr/share/relay/shell/event.py /usr/share/relay/scripts/relay-open /usr/share/relay/theme/terminal.conf \
    /usr/share/applications/org.relayterminal.Relay.desktop /usr/share/metainfo/org.relayterminal.Relay.metainfo.xml \
    /usr/share/icons/hicolor/scalable/apps/org.relayterminal.Relay.svg \
    /usr/share/icons/hicolor/256x256/apps/org.relayterminal.Relay.png; do
  [[ -e $f ]] || { echo "missing $f" >&2; exit 1; }
  echo "ok $f"
done
[[ -x /usr/share/relay/scripts/relay-open ]] || { echo "relay-open is not executable" >&2; exit 1; }

step "backend bytecode (#TZWF)"
# The installed tree is root-owned, so the worker can never write __pycache__ itself: without the
# install rule's compileall every worker start recompiles 65 modules (259 ms to ready instead of
# 71, nine times over for a three-tab session). The .pyc must be hash-based and name its installed
# path, so no mtime and no staging directory can decide whether it is used.
pycache=/usr/share/relay/backend/relay_core/__pycache__
pyc=$(ls "$pycache"/provider.*.pyc 2>/dev/null | head -n 1) ||:
[[ -n ${pyc:-} ]] || { echo "no $pycache: the package ships no bytecode" >&2; exit 1; }
python3 - "$pyc" <<'PY'
import importlib.util, marshal, sys
raw = open(sys.argv[1], "rb").read()
assert raw[:4] == importlib.util.MAGIC_NUMBER, "the .pyc was built by another Python"
flags = int.from_bytes(raw[4:8], "little")
assert flags & 1, f"the .pyc is not hash-based (flags {flags})"
name = marshal.loads(raw[16:]).co_filename
assert name == "/usr/share/relay/backend/relay_core/provider.py", name
print(f"ok {sys.argv[1]} (hash-based, {name})")
PY
python3 - <<'PY'
import importlib.util, pathlib, sys
missing = [str(p) for p in pathlib.Path("/usr/share/relay/backend").rglob("*.py")
           if not pathlib.Path(importlib.util.cache_from_source(str(p))).exists()]
if missing:
    sys.exit("not compiled: " + ", ".join(missing[:5]))
print("ok every installed backend module has its bytecode")
PY

step "relay --version / --help"
QT_QPA_PLATFORM=offscreen relay --version
help=$(QT_QPA_PLATFORM=offscreen relay --help); head -n 5 <<<"$help"

step "emulator core: the package ships libghostty-vt"
# VtCoreFactory lists ghostty first when it is compiled in, so it is the default core; the
# XTVERSION reply "Relay(libghostty-vt)" exists only in GhosttyCore.cpp, so its presence in
# the stripped binary is the proof. --engine-core=ghostty cannot be: an unknown core falls
# back to the default silently (engine/session/TerminalSession.cpp).
if grep -qF 'Relay(libghostty-vt)' /usr/bin/relay; then
  echo "ok /usr/bin/relay carries the libghostty-vt core (default core: ghostty)"
elif [[ ${RELAY_WITH_GHOSTTY:-1} == 0 ]]; then
  echo "libvterm-only package (RELAY_WITH_GHOSTTY=0)"
else
  echo "/usr/bin/relay has no libghostty-vt core: the package would run on libvterm" >&2; exit 1
fi

step "backend worker protocol"
reply=$(echo '{"type":"shutdown"}' | timeout 20 python3 -S /usr/share/relay/backend/worker.py)
echo "$reply"
grep -q '"event": *"ready"' <<<"$reply"

step "GUI start under Xvfb (offscreen and xcb)"
# Seconds the app gets to start. Raise it under emulation (e.g. RELAY_SMOKE_SECONDS=60 in an
# amd64 container on an arm64 host): xvfb-run alone waits 3 seconds for the X server.
wait_s=${RELAY_SMOKE_SECONDS:-10}
export LANG=C.UTF-8 LC_ALL=C.UTF-8  # xwininfo cannot print the "Relay — dir" title otherwise
gui_check() {
  local platform=$1 log home
  log=$(mktemp); home=$(mktemp -d)
  # A startup failure shows a modal dialog and keeps running, so "still alive at the timeout"
  # alone proves nothing. Instead check that the terminal engine spawned Relay's Bash
  # integration shell and that the agent worker started, and (xcb) that the window is mapped.
  HOME=$home QT_QPA_PLATFORM=$platform RELAY_SMOKE_SLEEP=$((wait_s - 4)) timeout "$wait_s" \
    xvfb-run -a -s '-screen 0 1280x800x24' \
    bash -c 'relay --workspace /tmp & app=$!; sleep "$RELAY_SMOKE_SLEEP"
             echo "--- processes"; ps -eo pid,ppid,args | grep -E "integration.bash|worker.py" | grep -v grep
             if [ "$QT_QPA_PLATFORM" = xcb ]; then echo "--- windows"; xwininfo -root -tree | grep -F "(\"relay\" \"relay\")"; fi
             wait $app' >"$log" 2>&1
  local status=$?
  pkill -x relay 2>/dev/null; pkill -f integration.bash 2>/dev/null; pkill Xvfb 2>/dev/null; sleep 1
  echo "[$platform] exit status $status (124 = still running at timeout)"
  grep -v -e 'Detected locale' -e 'UTF-8 locale' -e 'reconfigure your locale' -e 'for more information' "$log" | tail -n 25
  [[ $status == 124 ]] || return 1
  grep -q 'integration.bash' "$log" || { echo "[$platform] no Bash integration shell: the terminal engine did not start" >&2; return 1; }
  grep -q 'worker.py' "$log" || { echo "[$platform] agent worker did not start" >&2; return 1; }
  if [[ $platform == xcb ]]; then
    grep -q '"Relay could not start"' "$log" && { echo "[xcb] startup error dialog shown" >&2; return 1; }
    grep -Eq '"Relay( — [^"]*)?": \("relay" "relay"\)' "$log" || { echo "[xcb] no Relay main window" >&2; return 1; }
  fi
  return 0
}
set +e
gui_check offscreen; off=$?
gui_check xcb; xcb=$?
set -e
[[ $off == 0 && $xcb == 0 ]] || { echo "GUI smoke test FAILED (offscreen=$off xcb=$xcb)" >&2; exit 1; }
echo "smoke test passed"
