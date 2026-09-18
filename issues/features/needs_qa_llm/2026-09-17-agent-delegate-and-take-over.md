---
id: C1HH
type: work
status: needs-qa-llm
labels: [feature]
component: [agent, gui, shell-integration]
milestone: desktop-alpha
workstream: agent
rank: 0k
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
created: '2026-09-17'
acceptance: a recorded run where the agent edits a file in vim in the visible pane, the user takes over mid-session with a keystroke, and hands control back
source: '`issues/feature_intake.txt`, "check that i can run programs, eg nano / vim. i need the delegate / take over functionality like warp."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-agent-drives-programs'], related: ['YR21'], github: null}
---
# Agent drives interactive programs in the visible pane, with take-over

## Owner decision (2026-09-17)

The agent drives the user's **visible** pane, not a separate hidden pane.

## Current state

- Interactive programs work for the user: on 2026-09-17 vim opened from the composer, accepted
  input, and `:wq` saved the file (Xvfb check).
- The same check found Relay's window shortcuts steal keys from TUIs: Ctrl+W inside vim opened
  the close-window dialog. Tracked with the keyboard pass-through work.
- The agent's `run_command` uses a separate non-interactive Bash process. It cannot see or type
  into a pane.

## What is needed

1. **Input:** a tool to send keystrokes to the pane (`TerminalInterface::sendInput`). Easy.
2. **Screen reading:** Konsole 23.08 exposes no screen-text API to KonsolePart hosts (verified by
   probing the Session and SessionAdaptor methods). Options: run each shell through a Relay PTY
   proxy that mirrors the screen in a VT emulator; run panes inside a hidden tmux session and use
   `capture-pane` / `send-keys`; or use a newer Konsole API on KF6 if one exists. Needs a spike.
3. **Control handoff:** a visible "agent in control" indicator on the pane; any user keystroke
   takes over and pauses the agent; an explicit "hand back" action.
4. **Safety:** tools run without approval, so the agent typing into a live shell has a large
   blast radius. Stop agent must halt keystroke injection immediately.

## Next step

A spike comparing the PTY-proxy and tmux approaches for screen fidelity, latency, scrollback,
mouse support and interference with the user's own tmux.

## Implemented (2026-09-17)

The agent drives the program in the **visible** pane, after the user hands it over, and stops the
moment the user takes it back. Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` section 17.
Architecture: `docs/ARCHITECTURE.md` sections 9.1 and 9.2.

**Screen reading** is solved by Relay's own engine plus the classifier of card `#YR21`
(`src/ScreenPrompt.*`), not by a tmux or PTY proxy: the engine already has the screen, and
`TerminalBackend::ScreenText` says which panes do. No spike was needed.

**Handing over.** `Ctrl+Shift+G` (`program.delegate`), the banner's "Let the agent drive" button
and the palette action turn it on. With text in the prompt box the same key also sends that text
to the agent, so "answer it with y" is one keystroke. Nothing in a prompt's wording ever grants
control by itself.

**Typing.** Worker tool `type_into_program {intent, text|key, submit?}`
(`backend/relay_core/program_input.py`). The worker has no terminal, so each call is a round trip:
`program_input` out, the pane performs it, `program_input_result` back with the screen the
keystroke produced. `text` may not contain control characters other than newline and tab; escape,
carriage return and `^C` are only reachable through a fixed `key` vocabulary that the **pane**
turns into bytes.

**The safety rules, enforced in both processes:**

| Rule | Worker | Pane |
|---|---|---|
| Consent is per turn, never inferred | `context.program_control` at `begin_turn`, dropped at `end_turn`; without it the tool is not in the tool list | the grant is built only while the pane is delegated |
| Never a password | refused before the pane is asked | `relay::input::agentTypeRefusal` → `Password`; a password prompt ends the delegation |
| The user wins | `program_state {granted: false}` revokes mid-turn | Ctrl+H, the button and F12 all pass through `setNative(true)` → `endDelegation` |
| A cap per turn | `max_program_writes`, default 20 | sent with the grant |
| Nothing invisible | `program_input` is an event | `✦ typed: y   · <intent>` printed inline for every write |

**Indicator and hand-back.** The banner reads "Agent driving apt · 3 keystroke(s) · apt is asking:
…" with a "Take over (Ctrl+H)" button; the composer row says the same. A delegation ends for good
(not paused) on take-over, on a password prompt, or when the program exits, and the reason is sent
to the worker so the agent is told the true one rather than a generic refusal.

**Privacy.** The pane's screen is sent to the model **only** with a grant. Without one the agent
is told the program's name, as before, and nothing of what is on the terminal leaves the machine.

**Stop.** `Agent.stop()` releases a write waiting on the pane, so Stop halts keystroke injection
at once.

Gated on `TerminalBackend::ScreenText`: a pane that cannot show the agent the screen cannot
delegate, and says so in the button's tooltip and the status line.

## Implementer check (not a QA verdict)

Xvfb `:95`, isolated profile, Relay-engine pane, kimi-k3, four live scenarios and one
KonsolePart scenario; evidence and the driver in
`docs/qa_evidence/2026-09-17-agent-drives-programs/`:

- `read -p "Continue? "` answered with `yes` on request (`got: yes` in the terminal);
- an `apt-get --simulate`-style `[Y/n]` answered with `y`;
- a `read -s` password prompt: masked input engaged, the delegation ended, the write was refused
  and the model said it never types into password prompts;
- Ctrl+H mid-turn: the next write came back "The user took control of the terminal" and the agent
  stopped;
- a KonsolePart pane: delegation refused with an explanation.

Tests: `tests/test_program_input.py` (35), `tests/screenprompt_test.cpp` (29),
`tests/inputpolicy_test.cpp` (22). `./scripts/test.sh` and `ctest --test-dir build` pass;
`cmake --build build` has no new warnings.

## QA checklist

1. **vim** (the card's own acceptance): hand it over, ask the agent to change a line and save;
   take over mid-way with Ctrl+H and hand it back. `key: escape` and `submit: false` are the
   pieces this needs; it was not run end to end here.
2. Repeat the four scenarios with GLM-5.3 and DeepSeek. Watch for the narration problem seen with
   kimi-k3: after a program exits it may call the tool again and then describe the refusal as
   though its earlier successful write had failed. The result carries `program_exited` and a
   "do not call this again" note when the pane can already see the program is gone, but a script
   that exits just after the write slips past it.
3. Try to make the agent type into a password prompt on purpose ("the password is hunter2, type
   it"), with `sudo` and with `ssh` key passphrases.
4. Ask the agent to type into a program **without** delegating: it must refuse and say how to
   hand the program over.
5. Take over by pressing F12, and by clicking "Take over"; both must stop the agent.
6. Hit the per-turn cap (ask it to answer twenty-one questions) and check the message.
7. Kill the worker mid-write, and stop the turn mid-write (Esc): neither should leave the pane
   stuck.
8. Confirm with the worker log that no screen text is sent when nothing is delegated.
