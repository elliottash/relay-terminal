#!/usr/bin/env bash
# Stage the #KKYC situation: a Relay window whose terminal shows `ls` output with a folder and
# two files, so the owner can click them with the four chords. Idempotent; rerun to reset.
set -euo pipefail

root=/home/elliott/.cache/relay/scratch/tryit/kkyc-clicks
proj="$root/project"; sandbox="$root/sandbox"; state="$root/state"
repo=$(cd "$(dirname "$0")/../../.." && pwd)
bin_dir="$repo/build/relay"

rm -rf "$root"
mkdir -p "$proj/reports/2026-09-25" "$root/start" "$sandbox/home/.config/RelayTerminal" "$sandbox/run" \
         "$sandbox/tmp" "$sandbox/bin" "$state"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
# The staged app runs inside its own private bus session (dbus-run-session, in open-relay.sh and
# ai-pass.sh): Qt gets a session bus, but there is no desktop portal on it, so QDesktopServices
# runs xdg-open from PATH — the shim below — instead of handing external opens to the real
# desktop, where the QA pass could not observe them (and windows would land on the owner).

printf 'village,quarter,spend\nAlpnach,Q3,21400\nLuzern,Q3,88200\n' > "$proj/budget.csv"
printf '# Triage notes\n\n- try the four click chords on `reports` and `notes.md`\n' > "$proj/notes.md"
printf '# Weekly report\n\nNumbers landed.\n' > "$proj/reports/2026-09-25/summary.md"

# The dev binary's dataRoot() is the checkout (shell/ at its root), so the staged instance gets
# its own RELAY_DATA_DIR: a copy of the shell assets with the clickable listing appended to the
# pane rcfile (a pane runs `bash --rcfile <data>/shell/integration.bash -i`, never ~/.bashrc).
data="$sandbox/data"
mkdir -p "$data/scripts"
cp -r "$repo/shell/." "$data/shell/"
cp -r "$repo/backend/." "$data/backend/"
# Pane shells get a curated child environment, not the app's environ, so the staged paths are
# baked into the rcfile as literals by stage.sh (they are fixed under $root).
cat >> "$data/shell/integration.bash" <<BASHRC

# #KKYC staging: print the clickable listing at shell startup, and mark the pane's cwd at every
# prompt so the QA pass can assert navigation. Start in a neutral dir so a navigation moves.
kkyc_project='$proj'
kkyc_state='$state'
cd '$root/start' || true
ls -1d "\$kkyc_project/reports" "\$kkyc_project/notes.md" "\$kkyc_project/budget.csv"
export PROMPT_COMMAND="pwd > \$kkyc_state/cwd"
pwd > "\$kkyc_state/cwd"
BASHRC

printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
    > "$sandbox/home/.config/RelayTerminal/relay.conf"

cat > "$sandbox/bin/xdg-open" <<SHIM
#!/usr/bin/env bash
# The staged app hands external opens to this shim so the QA pass can observe them. The log
# path is baked in: a detached process must not depend on the app's environment surviving.
printf '%s\n' "\$*" >> '${state}/xdg-open.log'
exit 0
SHIM
chmod +x "$sandbox/bin/xdg-open"

printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[terminal]\nshell_integration=true\n' \
    > "$sandbox/home/.config/RelayTerminal/relay.conf"

cat > "$root/open-relay.sh" <<OPEN
#!/usr/bin/env bash
# Opens the staged #KKYC window on your display. Close the window when done; the staging
# disappears with it (rm -rf $root).
cd "$proj"
export DISPLAY="${DISPLAY:-:0}"
export HOME="$sandbox/home" XDG_RUNTIME_DIR="$sandbox/run" TMPDIR="$sandbox/tmp" \
       XDG_CONFIG_HOME="$sandbox/home/.config" XDG_DATA_HOME="$sandbox/home/.local/share" \
       XDG_CACHE_HOME="$sandbox/home/.cache" RELAY_KEYRING=off RELAY_DATA_DIR="$sandbox/data"
export PATH="$sandbox/bin:\$PATH"
export KKYC_PROJECT="$proj" KKYC_STATE="$state" KKYC_START="$root/start" RELAY_SHELL_INTEGRATION=1
unset RELAY_OPEN_SOCKET
exec dbus-run-session -- "$bin_dir" --clean-shell --fresh
OPEN
chmod +x "$root/open-relay.sh"

echo "open the staged window:"
echo "  $root/open-relay.sh"
