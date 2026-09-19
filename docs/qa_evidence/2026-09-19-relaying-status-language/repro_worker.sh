#!/usr/bin/env bash
# Reproduce the pane worker's instant exit (relay.log: worker_exit code=1 ~50 ms after
# worker_start) under the same sandbox environment drive.sh builds, with stderr visible —
# the GUI discards worker stderr by design (src/Pane.h connectWorker), so the traceback
# never reaches relay-stderr.log.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
root=$(cd ../../.. && pwd)

box=$(mktemp -d /tmp/relay-4e13-repro.XXXXXX)
mkdir -p "$box/home" "$box/run" "$box/tmp" "$box/home/project"
chmod 700 "$box/run"

# Exactly prepare()'s exports, plus start()'s additions.
export HOME=$box/home XDG_RUNTIME_DIR=$box/run TMPDIR=$box/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export RELAY_KEYRING=off
export RELAY_PANE_ID=repro RELAY_LOG_LEVEL=info

cd "$box/home/project"
timeout 5 python3 -S -u "$root/backend/worker.py" </dev/null
status=$?
echo "worker exit=$status"
rm -rf "$box"
exit $status
