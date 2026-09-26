---
id: XQ8F
type: work
status: needs-verification
labels: [feature, remote, ssh, terminal]
assignee: agent
implemented_by: glm/glm-5.3-flash
session: e3b30db3-cc88-44a2-a967-283322c7b4f5
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe, ai-text], human: optional, criteria: 'ssh to a host from a Relay pane, start `top`, quit Relay, start it again: the pane is back at the same session with top still running; Ctrl+E from that pane opens a second pane logged in to the same host in the same directory without a password', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Claude Code guest session in Relay, 2026-09-24, follow-up to #VD2M'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-persistent-panes-XQ8F/], related: [VD2M, S5SH, S7KC], github: null}
---
# Persistent remote panes: every ssh pane survives disconnects and restarts, Ctrl+E from a remote pane opens another pane on the host

## Issue
ok this is informative. it works. but i am thinking, we want to integrate this functionality more directly into relay. like, an ssh pane should be persistent by default. and when are you in an ssh pane, ctrl+E creates another remote pane.

think about the functionality we get with mosh and zellij that could be intergrated directly

and when i say integration, i meant we replicate the useful functionality (persistence / panes / etc in remote) without necessarily using zellij or mosh

i think we dont need zellij actually, we already have it. we just need liek remot mosh tabs

yes, put this on a card and deliver it

use subagents for implementation

## Decisions
- Owner, 2026-09-24: "an ssh pane should be persistent by default. and when are you in an ssh pane, ctrl+E creates another remote pane."
- Owner, 2026-09-24: "we replicate the useful functionality (persistence / panes / etc in remote) without necessarily using zellij or mosh" and "we dont need zellij actually, we already have it. we just need liek remot mosh tabs" — Relay's own tabs and panes are the only UI; nothing of a multiplexer's chrome shows.
- Owner, 2026-09-24: "yes, put this on a card and deliver it" / "use subagents for implementation" — the design below as proposed in the pane, accepted.
- Agent (design, accepted with the above): mosh alone cannot re-attach after the client dies (mosh's own FAQ), so a session that comes back after a Relay restart needs a holder on the host. The holder is a bare tmux on Relay's own socket (`tmux -L relay`, status off, prefix None, passthrough on), then screen, then a plain login shell — invisible, one session per Relay pane. tmux is preferred over zellij because it passes Relay's marks through; zellij passes nothing (#VD2M). The link is ssh first, mosh when the option asks for it and both sides have it; under mosh the marks travel through OSC 52, which mosh forwards.

## Done means
- `ssh <host>` typed in a Relay pane (or Connect to host…) lands in a tmux session on the host named after the pane, with no tmux chrome visible; the prompt marks, remote cwd and agent host tools work as in a plain ssh pane. Closing the pane or quitting Relay leaves the session and its programs running on the host; starting Relay again re-attaches every remote pane where it was, and reopening a closed remote tab re-attaches too.
- Ctrl+E (any `pane.split*`) from a remote pane opens the new pane logged in to the same host, in the source pane's remote directory, in its own session, with no password over the shared connection; "New local pane" is the escape hatch, and "Close and end the remote session" ends the session on the host.
- Options › Terminal › SSH gains "Persistent sessions" (on by default, off, honouring the never-enhance host list) and "Link" (ssh, or mosh when both sides have it). Under mosh the marks still arrive (rerouted through OSC 52) and the shared master does not expire while the pane is open.
- Hosts with no tmux fall to screen, then to a plain login shell with one line saying the session will not persist; nothing is installed on any host.
- Failure looks like: tmux status bar or prefix key visible inside the pane; a restart bringing back a local shell instead of the session; a split from a remote pane opening a local pane; a `ssh host cmd`, `ssh -N`, `-W`, `-O` line being wrapped into a session; marks silently gone under mosh.

## Plan
**Goal.** A remote pane is a Relay pane whose shell lives on the host in a session Relay names after the pane, so it survives the link, the pane and Relay itself; Relay's panes and tabs stay the only UI.

**Findings.**
- Every Relay-initiated connect already goes through the pane shell's `ssh`/`mosh` wrapper (`shell/integration.bash`, `RELAY_SSH_WRAP=1`): `connectToHost` queues `relay::ssh::connectCommand` (`ssh <target>`), Split on the same host queues `relay::ssh::rerunCommand(argv)`, so persistence implemented **once in the wrapper** covers typed ssh, the host picker, splits and restore alike.
- `Pane::scrollbackId()` is a stable per-pane id that the saved layout and "restore last closed" carry (`src/RelayWindow.h` serializeNode, `src/Pane.h` ~1267): the session name derives from it, so a restored pane regenerates the same name. The layout leaf's `queue` is restored into the pane's queue (waits for the person to resume it).
- `src/PaneRuntime.cpp` ~672 exports `RELAY_SSH_WRAP`/`RELAY_SSH_DIR` from Options `ssh/enhance`; `src/RelayWindowSettings.cpp` ~330 holds the SSH option rows; `src/Pane.h` `maybeEnhanceLogin`/`typeLoginBootstrap` (~15040) type `shell/remote-integration.sh` via `relay::remote::bootstrapLine` and skip mosh because mosh drops OSC.
- The engine's OSC 52 path (`engine/core/GhosttyCore.cpp` onClipboardWrite, `engine/core/LibVtermCore.cpp` ~912) is gated by `clipboardAllowed`; mosh 1.4 forwards OSC 52, so a `relay:`-prefixed OSC 52 payload is the mark channel under mosh.
- Local facts: tmux 3.4 accepts `prefix None`; mosh 1.4.0 and an active sshd are on this machine for end-to-end runs.

**Steps** (one subagent per lettered area; files are disjoint).
1. **A — holder + wrapper.** New `shell/remote-holder.sh` (no single quotes; `$1` session, `$2` cwd): writes `~/.cache/relay/tmux.conf` (status off, prefix None, mouse off, set-clipboard on, allow-passthrough on, window-size latest, history 50000, escape-time 10) and runs `tmux -L relay -f <conf> new-session -A -D -s $1 -c $2`, else `screen -D -R -S $1`, else prints one line and `exec $SHELL -l`. `shell/integration.bash`: when `RELAY_SSH_PERSIST=1` and the argv is a plain interactive login (destination, no remote command, none of -N -W -O -G -V -Q -s -T -f -M, host not in the never list passed as `RELAY_SSH_NEVER`), `ssh` runs `ssh -t <args> sh -c '<holder>' relay-holder <session> <cwd>` with session `relay-${RELAY_PANE_ID:0:8}` (or `$RELAY_SSH_SESSION`), cwd `$RELAY_SSH_CWD`; `RELAY_SSH_LINK=mosh` with a local mosh runs `mosh <args> -- sh -c …` and falls back to the ssh form when mosh dies within 20 s. Tests in `tests/test_ssh_shell.py` with the fake ssh (argv shape, every gate) and with real tmux (`-L relay-test`) for the holder script.
2. **B — rules.** `src/SshConfig.{h,cpp}`: `rerunCommand` strips `-t` and the holder remote command (`sh -c … relay-holder …`, for ssh and mosh forms) so a rerun goes through the wrapper fresh; `holderSession(argv)`; `killSessionCommand(session)`, `listSessionsCommand()`, `parseSessionList(output)` (`tmux -L relay ls -F '#{session_name}\t#{session_created}\t#{session_attached}'`); retire `persistentCommand`/`persistentSession`. `tests/sshconfig_test.cpp`.
3. **D — marks over mosh.** `shell/remote-integration.sh`: with `RELAY_M=1`, `__relay_r_o` emits `\e]52;c;<base64 of relay:<osc>>\a` (inside the tmux DCS when under tmux). `src/RemoteSession.{h,cpp}`: `bootstrapLine(…, bool viaMosh=false)` adds `RELAY_M=1`. Engine: a decoded OSC 52 write whose text starts with `relay:` is dispatched as that OSC (133/7/777/7772) in both cores regardless of `clipboardAllowed` and never reaches the clipboard. Tests: `engine/tests/CoreTest.cpp`, `tests/remotesession_test.cpp`, new `tests/test_remote_marks.py`.
4. **C2 — pane runtime** (after B, D). `src/PaneRuntime.cpp`: export `RELAY_PANE_ID` (scrollbackId), `RELAY_SSH_PERSIST`, `RELAY_SSH_LINK`, `RELAY_SSH_NEVER` from Options. `src/Pane.h`: enhance mosh logins too, passing `viaMosh` to `bootstrapLine`; while a mosh login with a reachable control path is active, run `ssh -S <cp> -o ControlMaster=no <host> true` every 4 min so the master does not expire; a login whose argv holds the holder marker shows "persistent" on the remote chip and exposes `holderSession()`.
5. **C1 — window** (after B). `src/RelayWindow.h`/`RelayWindowCore.cpp`/`RelayWindowSettings.cpp`/`Keymap.h`: `pane.split*` from a pane with a remote login splits in that direction and queues `RELAY_SSH_CWD=<remote cwd> <rerunCommand>`; new `pane.splitLocal` ("New local pane"); `ssh.connectPersistent` retired; `pane.closeEndRemote` ("Close and end the remote session": kill over the master, then close) in the pane menu and Actions; `ssh.remoteSessions` ("Remote sessions on this host…": list over the master, Enter opens a pane with `RELAY_SSH_SESSION=<name> ssh <host>`); serializeNode adds the rerun line to the leaf's `queue` for a pane with a live remote login so restore/reopen re-attach; Options rows Persistent sessions (on/off) and Link (ssh/mosh).
6. **Docs + evidence** (me): `docs/SSH-AND-MOSH.md` section 12 replaces section 11's persistent recipe; `docs/qa_evidence/2026-09-24-persistent-panes-XQ8F/` with the test runs and an end-to-end run against localhost.

**Risks.** `src/Pane.h` and `src/RelayWindowCore.cpp` carry other sessions' uncommitted hunks: C1/C2 land with `land.py` hunk selection. A host whose login shell is restricted or has ForceCommand gets `-t` + a command it may refuse: the gate above and the never-list are the guard, and the plain-shell fallback inside the holder covers a host with neither tmux nor screen. Restored queues wait for the person to resume: the reattach line is therefore visible and one keypress away rather than automatic (kept: a queue must not run on its own). Decision for the owner if they disagree: tmux preferred over zellij for the holder (marks), and `-D` detaching any other client on attach.

**Verify.** `python3 tests/test_ssh_shell.py`, `ctest --test-dir build -R '^(sshconfig|remotesession|keymap)$'`, `engine` CoreTest, `tests/test_remote_marks.py`; then by hand against localhost: ssh in, `top`, quit Relay, relaunch, resume the restored line, top is still there; Ctrl+E opens a second pane on the host in the same directory.

## Tasks

- [x] A: shell/remote-holder.sh + wrapper persistence + tests/test_ssh_shell.py — 3c67c241, mosh quoting fix ddc60ef4 <!-- t:de -->
- [x] B: SshConfig rules (rerun strips holder, session parsing, kill/list) + sshconfig_test — ec0e5a1c <!-- t:a3 -->
- [x] D: marks over mosh via OSC 52 (remote-integration.sh, RemoteSession, engine cores) + tests — 8c0d9f43 <!-- t:h7 -->
- [x] C2: PaneRuntime env exports, restore runs remote_login, mosh enhance + keepalive, persistent chip — bcab1720, 3e7dce48 <!-- t:2k blocked_by=a3,h7 -->
- [x] C1: RelayWindow split-on-host, New local pane, close-and-end, remote sessions list, remote_login in the layout leaf, Options rows — 7f778cc7, 16763f52 <!-- t:9w blocked_by=a3 -->
- [x] Docs section, evidence dir, end-to-end run, card to needs-verification — f4ec5642, 59aa3ad8 <!-- t:4n blocked_by=2k,9w -->

## Execution Summary
Taken over from the stopped Codex session and finished 2026-09-25. Landed on main: bcab1720 (pane runtime: wrapper env, restore re-attach, mosh keepalive, persistent chip), 3e7dce48 + 16763f52 (window: split-on-host from a remote pane, New local pane, Close and end the remote session, Remote sessions on this host, remote_login in the layout leaf, Options rows Persistent sessions and Link), f4ec5642 (docs section 11 rewritten), 7f778cc7 (holder: screen fallback added, quoting of remote cwd with spaces, zsh -d in tests), 59aa3ad8 (QA evidence: staged GUI run under xvfb + full test suites). screen fallback verified against a real screen session (tests/sshconfig_test.cpp); end-to-end run staged against localhost over ssh: login, split on host, Relay restart with the session re-attached (`top` still running). Evidence: docs/qa_evidence/2026-09-24-persistent-panes-XQ8F/.

## Tests
- python3 tests/test_ssh_shell.py — 41 tests pass (wrapper gates, holder over real tmux and screen, marks over mosh end-to-end).
- python3 tests/test_remote_marks.py — 6 tests pass, including a real mosh login carrying marks through OSC 52.
- ctest ^(sshconfig|remotesession|keymap|backends|relay-engine-tests)$ — all pass (evidence: tests.txt in the evidence dir).
- Staged GUI run (xvfb): ssh to localhost, start top, quit and restart Relay — session re-attached with top running; Ctrl+E opened a second pane on the host in the same directory; screenshots 01–03 in the evidence dir.
- Working tree on the land-verify slot of each commit built the exact landed tree (land.py build gate).
