---
id: 87HB
type: work
status: needs-verification
labels: [feature, design]
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: glm/glm-5.3
session: b74e0c22-ed30-4b41-bf11-e19255da698a
priority: -1
rank: zzzzzm
created: '2026-09-18'
verify: {artifact: system, primary: person, also: [script], human: none, criteria: 'Done means: a live local process survives Relay quitting and returns with its pane (screen, cwd, pre-restart scrollback replayed first); close-and-end empties its session; the sessions list sees leftovers; option off behaves exactly as today; marks/prompt/cwd tracking intact in a wrapped pane.', sign_off: none, effort: medium, stakes: rework}
source: owner, in a Claude Code session, 2026-09-18, while deciding how
links: {commits: [0fbf2cac, 7beab862, 9f1032d9], evidence: [docs/qa_evidence/2026-09-25-87hb-local-holder/], github: null, plans: [docs/SSH-AND-MOSH.md], related: [S5SH, SPBN, XQ8F, VD2M]}
---
# The local half of the tmux gap: a shell that survives Relay quitting

## Issue

> do the small fix, i dont use tmux that often, but other users will. something i would also like to
> file as a feature request, is if there are some tmux like features we can implement directly
> through relay terminal.

(The "small fix" is #S5SH's: Relay's remote integration wraps its escape sequences so they survive a
tmux on the host.)

## What tmux is actually used for, and where Relay stands
Compared against the mosh work on 2026-09-25 (owner's ask). Splits, tabs, scrollback, search and
session naming Relay already has — the gap was always **a shell that outlives the window**, in two
places, and one of the two is now built:

- **Remote: delivered, awaiting verification in #XQ8F.** Every ssh pane is persistent by default —
  a bare `tmux -L relay` holder on the host (screen fallback, plain shell last), invisible chrome,
  session named per pane, re-attached after a Relay restart, Ctrl+E splits on the host, mosh link
  optional. That is option 3 below, which this card called "the cheap experiment" — it shipped.
  #VD2M tier A (zellij inside Relay) and #234Z (Alt+Esc leaves a session) sit beside it.
- **Local: still missing, and what this card now holds.** A pane's shell is a child of Relay: when
  Relay exits or crashes, every local command dies with it. The layout, directory and scrollback
  text come back (`src/WindowState.h`); the processes do not. #XQ8F's holder runs on hosts only.
- **Detach here, attach there between two desktops**: still open. The phone share covers watching;
  two desktops cannot share a pane.

## Options
1. **A local session keeper.** Relay stops owning the pty directly: each pane's shell is started by a
   small helper (one process per pane, or one daemon for all of them) that owns the pty and keeps a
   ring buffer of output. Relay attaches over a Unix socket in `$XDG_RUNTIME_DIR`. Quitting Relay
   leaves the helper running (by choice, per pane or per tab); starting Relay offers to re-attach,
   with the live processes still there. A crash becomes recoverable instead of destructive.
   - Fits what is already built: `engine/pty/` is where the pty lives, the layout restore already
     knows which panes existed, and the scrollback restore already replays text.
   - Costs: a second process to ship and version, a protocol between it and the GUI, and careful
     handling of the terminal size and of orphaned helpers.
   - **Cheaper now (2026-09-25): #XQ8F proved the attach/detach model with a tmux holder. A local
     pane could run its shell under the same `tmux -L relay` holder locally — no bespoke daemon,
     no new protocol — trading the tmux dependency for a week of work instead of a quarter.**
2. **The same keeper on the host.** ~~Open~~ **Rejected in favour of 3** (2026-09-25): per-architecture
   builds and installing anything on a host, which the mosh discussion set aside — #XQ8F's holder
   uses the host's own tmux/screen and installs nothing.
3. **Use an existing keeper instead of writing one** — **shipped for the remote case in #XQ8F**
   (tmux on a Relay socket, invisible; screen fallback; nothing installed). The original warning
   (Warp's tmux experiment, docs/SSH-AND-MOSH.md "Research notes") was answered by hiding all
   multiplexer chrome and naming sessions after panes, not windows.
4. **`tmux -CC` control mode** — **stays rejected**: a large state machine that only helps people
   who already run tmux (Research notes, same doc).

## Open questions for the owner
- Is "quit Relay, come back, the local build is still running" worth having, now the remote half
  exists? (The third question below — does the remote case matter — was answered by #XQ8F: yes, and
  it shipped.) If yes, local holder (option 1 via #XQ8F's tmux) or nothing at all?
- Should re-attaching be automatic, or offered ("3 panes are still running")? #XQ8F chose offered:
  the restored queue waits for one keypress, never runs on its own.
- Detach here, attach there **between two desktops** — wanted, or is phone-sharing enough?

## Done means
A local pane's shell and whatever it is running survive Relay quitting or crashing: after a restart
the pane offers its re-attach line, and one keypress brings the session back with its processes,
cwd and visible screen (pre-restart scrollback lives in the holder's history, as remotely). Closing
a pane normally leaves its session; an explicit close-and-end kills it, and leftover `relay-*`
sessions are listable and killable. With the option off, a pane behaves exactly as today. Failure
looks like: processes gone after a restart, orphaned sessions nobody can see or end, or local panes
losing marks, prompt and cwd tracking once wrapped.

## Plan
Run a local pane's shell under the same holder #XQ8F uses on hosts — nothing new to invent, only
the local entry, the restore hook and the close semantics.

### Goal

A local pane's shell runs inside a session of the local `tmux -L relay` holder, named after the
pane's stable scrollback id, so quitting or crashing Relay leaves local commands running and a
restarted Relay re-attaches every pane where it was. No bespoke daemon, no new protocol.

### Findings

- `shell/remote-holder.sh` (#XQ8F) is host-agnostic: it writes `~/.cache/relay/tmux.conf` (status
  off, prefix None, mouse off, `allow-passthrough on`, history 50000), honours `RELAY_HOLDER_SOCK`,
  runs `tmux -L <sock> -f conf new-session -A -D -s <session> -c <cwd>`, falls back to screen, then
  to a plain `exec $SHELL -l`. Nothing in it knows it is remote.
- Local panes pass no program: `engine/pty/Pty.h` (`options.program` empty = `$SHELL`) and
  `engine/pty/PtyUnix.cpp:89`. The wrap must therefore happen where the pane builds its session —
  `src/PaneRuntime.cpp` `startTerminal` (~890), which already exports `RELAY_PANE_ID=scrollbackId()`
  (line 898) and `RELAY_SSH_PERSIST` (line 712).
- Marks survive the wrap for free: `shell/integration.bash` detects `$TMUX` and wraps OSC 133 in
  tmux passthrough (the #S5SH fix); tmux unwraps it to the pane. No engine change.
- Session naming is stable across restarts already: the saved layout keys panes by scrollback id
  (`src/RelayWindow.h` `serializeNode`, `remote_login` at ~6013; `src/WindowState.h`).
- Restore queues a re-attach line for remote panes: `src/PaneRuntime.cpp` ~2270 (`remote_login`
  in the leaf → resume line, one keypress, never automatic). A local pane needs the same leaf
  field and the same queue path.
- Close-and-end and the session list exist: `src/SshConfig.h` `holderSession`, `killSessionCommand`,
  `listSessionsCommand`, `parseSessionList`; the pane chrome switch is
  `src/PaneRuntime.cpp:2295` (`setRemotePersistent`). All are holder sessions, not ssh sessions —
  they generalise.
- The Options row to copy: `src/RelayWindowSettings.cpp:363` (`ssh/persist` "Persistent sessions").

### Steps

1. **Start under the holder.** In `startTerminal` (`src/PaneRuntime.cpp` ~890): when the new option
   `terminal/persistLocal` is on and the pane is a plain local shell (no explicit program, not a
   console), set the session program to `<shellDir>/remote-holder.sh relay-<scrollbackId-8> <cwd>`
   and export `RELAY_HOLDER_SOCK`/`RELAY_HOLDER_CWD` if the script prefers env over argv. The
   holder's fallback chain means no local tmux ⇒ today's plain shell plus one explanatory line.
2. **Environment inside the session.** Extend the tmux.conf the holder writes so the session shell
   inherits `RELAY_PANE_ID`, `RELAY_RUNTIME_DIR`, `RELAY_SHELL_EVENT` and the shell dir (tmux drops
   some of its client's environment); `$TMUX` then triggers the existing mark wrapping. Check the
   non-bash `$SHELL` path reaches integration the same way it does today — expect no change.
3. **Persist and restore.** Save `local_login` (session name) in the layout leaf beside
   `remote_login` (`src/RelayWindow.h` serializeNode ~6013); on restore (`src/PaneRuntime.cpp`
   ~2270) queue the same holder line as the pane's resume line — offered, one keypress, matching
   the remote behaviour the owner already accepted.
4. **Close semantics.** Default pane close leaves the session running; extend the existing
   close-and-end action and `setRemotePersistent` to local holder panes (`killSessionCommand` needs
   no change — the socket is local). Reopening a closed tab re-attaches.
5. **Session list.** Give the "sessions" list a local section (`listSessionsCommand` against
   `tmux -L relay ls` locally, `parseSessionList` as-is) so leftover `relay-*` sessions are visible
   and killable without a terminal.
6. **Option row.** `src/RelayWindowSettings.cpp` (~363): Terminal section row "Persistent local
   panes", bound to `terminal/persistLocal`, default on, wording parallel to "Persistent sessions".

### Orchestration

Single agent, no subagents: every step touches `src/PaneRuntime.cpp`, `src/Pane.h` or
`src/RelayWindow.h`, which are the contested files of this checkout.

### Risks

- **Default on — decided by the owner, 2026-09-25** ("On by default"): `terminal/persistLocal`
  ships on, symmetric with `ssh/persist`; the Options row is the escape hatch.
- Orphaned sessions accumulate from closed panes nobody returns to; mitigated by the list and
  close-and-end, and they die with the machine's reboot. No ageing policy in this card.
- Shared files: expect contested-hunk review at `land.py commit`; claim the paths first.
- Fold/clickable-path fidelity: pre-restart scrollback replays as text from `WindowState`, not as
  marked output — same boundary the remote case accepted; the pane's post-attach stream is marked
  normally.
- A user's own `~/.tmux.conf` is untouched (the holder uses `-f` its own), but a user-level tmux
  *server* on the default socket is a different server entirely — no interference expected, note
  in tests.

### Verify

- Extend `tests/test_ssh_shell.py` (already drives a real `tmux -L relay-test`): a local invocation
  of `remote-holder.sh` names the session `relay-<id>`, re-attaches with `-A -D`, and falls back
  cleanly with no tmux. Keep it in `## Tests` on the card.
- A restore test where one exists for `remote_login` (find the nearest in `tests/`): a leaf with
  `local_login` queues the resume line and does not start the shell on its own.
- By hand: `top` in a local pane → quit Relay → relaunch → resume → `top` alive in the same pane;
  close-and-end leaves `tmux -L relay ls` empty; the option off gives today's dying shell;
  marks, prompt and cwd tracking intact in a wrapped pane. `ctest --test-dir build -R sshconfig`
  for the untouched remote paths.

## Tests
Run 2026-09-25, this checkout, commits `0fbf2cac` + `7beab862`; full record in `docs/qa_evidence/2026-09-25-87hb-local-holder/README.md`.

- `scripts/relay-build` — green after every source change, and the `land.py` verify-slot build of the exact landed tree passed before the commit was swapped in.
- `ctest --test-dir build -R sshconfig` — 1/1 passed, now covering the local argv shapes (the `/bin/sh remote-holder.sh …` program, the exec'd `tmux -L relay … -s relay-x` client) and `localHolderName`'s first-eight-safe-chars reduction.
- `TMPDIR=/tmp python3 -m unittest discover -s tests -p test_ssh_shell.py` — 45/45 OK. New: `test_local_holder_runs_the_session_command` (session named, command runs in a new session with the passed environment, a second run attaches and does **not** re-run it), `test_local_holder_fallback_without_tmux` (explanatory line, no session command run), `test_local_holder_marks_reach_the_pane` (with `RELAY_HOLDER=1` the integration shell's OSC 133 reaches the pane through the real tmux; with 0 the raw mark dies inside it), `test_local_holder_restore_line_reattaches` (client dies, session lives, the saved line typed into a fresh shell re-attaches and redraws the session's screen).
- `WrapperTests` need `TMPDIR=/tmp` — under Relay's scratch TMPDIR their ssh ControlPath expectations differ; environmental, present before this card.

Not machine-checked (needs a person at the window): quit/relaunch with a live `top`, close-and-end by hand, the option-off escape hatch, marks/bands/cwd by eye.

## Execution Summary
Commits `0fbf2cac` (the feature, 14 paths) and `7beab862` (QA evidence). No subagents, per the plan's Orchestration.

1. **Start under the holder** — `startTerminal` runs a plain local pane's program as `/bin/sh <shellDir>/remote-holder.sh relay-<scrollbackId-8> <cwd> <session command>` when `terminal/persistLocal` is on (default). The holder script gained the optional third argument: the command a *new* session runs, ignored on re-attach. The session's shell is the same `relayBash() --rcfile integration.bash` the pane runs today. Panes under memory isolation keep their plain shell — a tmux server that outlives the pane would defeat the unit.
2. **Environment** — the pane's `RELAY_*` environment (PANE_ID, SESSION_TOKEN, RUNTIME_DIR, SHELL_EVENT, SHELL_INTEGRATION, CLEAN_SHELL, TMPDIR…) rides the session command line as quoted assignments, because one tmux server serves every pane and its own environment is the first client's. Deviation from the plan's wording (tmux.conf `update-environment`): the command line is deterministic per pane; the conf needed no change. `RELAY_HOLDER=1` marks the shell as holder-born.
3. **Marks** — the plan's "for free" was optimistic: `integration.bash` had no `$TMUX` handling. Both `integration.bash` and `relay-integration.bash` now wrap every OSC emission (133 A/C/D, OSC 7, 777 notify, 7772 row marks in BEL form) in tmux's DCS passthrough when `$TMUX` **and** `RELAY_HOLDER=1` are set — the #S5SH shape; the holder's conf sets `allow-passthrough on`, and tmux unwraps to the pane. A tmux of the user's own keeps today's raw behaviour, gate and all.
4. **Persist and restore** — `serializeNode` saves the pane's re-attach line as `local_login` beside `remote_login` (read from the pane's live foreground argv, so a pane the line re-attached answers as truly as one born wrapped). The `Pane` constructor takes the saved spec so a restored pane knows its line *before* `startTerminal` — otherwise it would attach at startup, before the scrollback and queue are back — and `runRestoredRemoteLogin` runs it automatically at the pane's first prompt, exactly as the shipped remote line does (the restored *queue* below still waits for its keypress). A `local_login` line restores even when the option is now off, symmetric with `remote_login`; the sessions list can kill what that leaves behind.
5. **Close semantics** — normal close leaves the session; the pane menu's "Close and end this pane's session" (local label of `pane.closeEndRemote`) kills it through the local socket and closes the pane on confirmation; `holderSession` learned the two local argv shapes, shared with `closeEndRemote`.
6. **Sessions list** — `ssh.remoteSessions` ("Persistent sessions…") now lists, reattaches and kills the local machine's holder sessions from a local pane, same dialog, same commands, through `sh` instead of ssh.
7. **Options row** — Terminal section, "Persistent local panes", `terminal/persistLocal`, default on (the owner's 2026-09-25 decision), aliases for search; `Keymap` descriptions updated to cover the local half.
