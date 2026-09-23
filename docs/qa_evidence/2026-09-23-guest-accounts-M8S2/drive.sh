#!/usr/bin/env bash
# Options › Models with two registered guest accounts (#M8S2), under Xvfb, in an isolated home.
# The `claude` and `codex` on PATH are the test fakes (tests/test_guest_accounts.py): they answer
# the login-status commands from a `signed-in` marker in the account's directory and make no
# network call. No real login is read and no model is asked anything.
set -euo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-23-guest-accounts-M8S2
sandbox=$(mktemp -d /tmp/relay-m8s2-XXXXXX)
display=:889
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 1640x940x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display HOME=$sandbox XDG_CONFIG_HOME=$sandbox/.config XDG_DATA_HOME=$sandbox/.local/share
export XDG_CACHE_HOME=$sandbox/.cache XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export RELAY_KEYRING=off RELAY_OPENROUTER_CATALOG=off RELAY_CODEX_BIN=
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$sandbox/project" "$sandbox/bin"
chmod 700 "$XDG_RUNTIME_DIR"
printf '# demo\n' > "$sandbox/project/README.md"
# The fakes, written out of the test module so the evidence runs exactly what the tests run.
PYTHONPATH=$root/backend:$root/tests python3 - "$sandbox/bin" <<'PY'
import os, stat, sys, json
sys.path.insert(0, os.environ["PYTHONPATH"].split(":")[1])
import test_guest_accounts as t, test_keytest_guest as k
d = sys.argv[1]
for name, body in (("claude", t.FAKE_CLAUDE), ("codex", t.FAKE_CODEX)):
    p = os.path.join(d, name)
    body = body.replace("#!PYTHON", "#!" + sys.executable, 1)
    if name == "claude":
        # `claude auth login` signs the account's directory in: the marker the status reads.
        body = body.replace('if sys.argv[1] != "-p":',
            'if sys.argv[1:3] == ["auth", "login"]:\n'
            '    open(os.path.join(os.environ.get("CLAUDE_CONFIG_DIR") or here, "signed-in"), "w").close()\n'
            '    print("Login successful."); sys.exit(0)\n'
            'if sys.argv[1] != "-p":', 1)
    open(p, "w").write(body)
    os.chmod(p, 0o755)
json.dump({**k.CLAUDE_STATUS, "loggedIn": True}, open(os.path.join(d, "claude-status.json"), "w"))
open(os.path.join(d, "codex-logged-in"), "w").close()
PY
export PATH=$sandbox/bin:$PATH
# Two accounts beside the default logins: Claude Code "work" signed in, Codex "eth" not yet.
mkdir -p "$XDG_CONFIG_HOME/relay/guest-accounts/claude-work" "$XDG_CONFIG_HOME/relay/guest-accounts/codex-eth"
touch "$XDG_CONFIG_HOME/relay/guest-accounts/claude-work/signed-in"
cat > "$XDG_CONFIG_HOME/relay/guest-accounts.json" <<JSON
{"accounts": [
  {"id": "work", "guest": "claude", "label": "work", "config_dir": "$XDG_CONFIG_HOME/relay/guest-accounts/claude-work"},
  {"id": "eth", "guest": "codex", "label": "eth", "config_dir": "$XDG_CONFIG_HOME/relay/guest-accounts/codex-eth"}
]}
JSON
cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[isolation]
enabled=false
[instructions]
onboarded=true
[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF
"$root/build/relay" --workspace "$sandbox/project" >"$sandbox/relay-stderr.log" 2>&1 & relay_pid=$!
sleep 12
win=$(xdotool search --pid "$relay_pid" | head -1)
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 900 windowfocus "$win"
sleep 4
import -window root "$out/00-start.png"
xdotool key ctrl+shift+m
sleep 8
import -window root "$out/01-models-pane.png"
xdotool mousemove 850 135 click 1
sleep 3
import -window root "$out/02-providers.png"
# Down to the signed-out Codex account.
xdotool mousemove 1200 600
for n in 1 2 3 4; do xdotool click 5; sleep 0.1; done
sleep 2
import -window root "$out/03-signed-out-account.png"
xdotool mousemove 1200 600
for n in 1 2 3 4; do xdotool click 4; sleep 0.1; done
sleep 2
# "add account…" on the default Claude Code row.
xdotool mousemove 1491 488 click 1
sleep 3
import -window root "$out/04-add-account-dialog.png"
# Name it and press OK: the worker registers it, the row appears, and the new directory's sign-in
# is typed into the pane's terminal (the fake CLI exits; nothing signs in).
xdotool type --delay 60 'personal'
xdotool key Return
sleep 6
import -window root "$out/05-account-added.png"
# The sign-in exited 0, so the pane asked the worker to check every login again: the new row
# reads "logged in" with no press on test.
xdotool mousemove 1200 600
for n in 1 2 3 4 5 6; do xdotool click 5; sleep 0.1; done
sleep 2
import -window root "$out/05b-signed-in-row.png"
xdotool mousemove 1200 600
for n in 1 2 3 4 5 6; do xdotool click 4; sleep 0.1; done
sleep 1
cp "$XDG_CONFIG_HOME/relay/guest-accounts.json" "$out/guest-accounts-after-add.json"
sed -i "s#$sandbox#<sandbox>#g" "$out/guest-accounts-after-add.json"
# The pane's model box: the same model through each login.
xdotool mousemove 585 865 click 1
sleep 3
import -window root "$out/06-model-box.png"
xdotool key Escape
sleep 1
xdotool key Escape
sleep 1
# The models pane's "available" tab: the selected row's "via" list names each login separately.
xdotool mousemove 932 135 click 1
sleep 3
xdotool type --delay 60 'opus'
sleep 2
import -window root "$out/07-available-via.png"
cp "$sandbox/relay-stderr.log" "$out/relay-stderr.log" 2>/dev/null || true
