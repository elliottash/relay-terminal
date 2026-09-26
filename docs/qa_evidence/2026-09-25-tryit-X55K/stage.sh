#!/usr/bin/env bash
# Stage the #X55K situation: a Relay console pane whose scrollback holds 400 numbered marker
# lines above the prompt, so Alt+Home / Alt+End can be judged with the naked eye.
# Idempotent; rerun resets. No model, no network.
set -euo pipefail

root=/home/elliott/.cache/relay/scratch/tryit/x55k-scroll
proj="$root/project"; sandbox="$root/sandbox"
repo=$(cd "$(dirname "$0")/../../.." && pwd)
bin="$repo/build/relay"

rm -rf "$root"
mkdir -p "$proj" "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
printf '# Scrollback fixture for #X55K\n' > "$proj/README.md"

# The dev binary's dataRoot() is the checkout, and a pane runs
# `bash --rcfile <data>/shell/integration.bash -i` (never ~/.bashrc), so the markers are
# appended to a private copy of the shell assets under a staged RELAY_DATA_DIR.
data="$sandbox/data"
mkdir -p "$data/scripts" "$data/backend"
cp -r "$repo/shell/." "$data/shell/"
cp "$repo/backend/worker.py" "$data/backend/worker.py" 2>/dev/null || true
cat >> "$data/shell/integration.bash" <<'BASHRC'

# #X55K staging: 400 numbered lines of scrollback above the first prompt.
printf 'SCROLLBACK-MARK %04d of 0400\n' $(seq 1 400)
BASHRC

printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
    > "$sandbox/home/.config/RelayTerminal/relay.conf"

cat > "$root/open-relay.sh" <<OPEN
#!/usr/bin/env bash
# Opens the staged #X55K window on your display. Close the window when done; the staging
# disappears with it (rm -rf $root).
cd "$proj"
export DISPLAY="\${DISPLAY:-:0}"
export HOME="$sandbox/home" XDG_RUNTIME_DIR="$sandbox/run" TMPDIR="$sandbox/tmp" \
       XDG_CONFIG_HOME="$sandbox/home/.config" XDG_DATA_HOME="$sandbox/home/.local/share" \
       XDG_CACHE_HOME="$sandbox/home/.cache" RELAY_KEYRING=off RELAY_DATA_DIR="$data"
unset RELAY_OPEN_SOCKET
exec "$bin" --clean-shell --fresh
OPEN
chmod +x "$root/open-relay.sh"

echo "open the staged window:"
echo "  $root/open-relay.sh"
