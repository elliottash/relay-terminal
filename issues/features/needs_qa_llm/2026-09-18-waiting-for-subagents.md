---
id: V7QD
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent (subagents)
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, in the shared checkout), 2026-09-18
rank: zzzzzz
created: '2026-09-18'
source: issues/feature_intake.txt, 2026-09-18
acceptance: '`tests/subagents_test.cpp` (ctest `subagents`), live run in `docs/qa_evidence/2026-09-18-waiting-for-subagents/`'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-waiting-for-subagents/], related: [QHR1, WD83], github: null}
---
# "waiting for N subagents . . ." in the prompt box

## Issue
if an orchestrator terminal is waiting on subagents, play a ... waiting for subagents . . . blinking
text in the prompt.

## Decisions

Implementer's reading (Claude Opus 5, 2026-09-18), to be confirmed or overruled by the owner:

- **"In the prompt" is the prompt box's own placeholder**, not a new widget and not a toast. It is
  the only text that is literally inside the prompt, and it has the behaviour the feature needs for
  free: Qt stops drawing a placeholder the moment a character is typed, so a steer is never
  obstructed, and `RichEditor::setPlaceholders` already picks the longest of several forms that fits
  a narrow pane. The strip's turn clock says the same thing beside the elapsed time — "waiting for 2
  subagents · 16 s · Esc stops" instead of "thinking · 16 s · Esc stops" — because a clock that says
  "thinking" while the agent is blocked is simply wrong.
- **Three ways the orchestrator is waiting, all derived from events that already exist.** No worker
  change was needed. Subagents must be live, and then any of:
  1. the main agent's running tool call is `agent_wait` (`tool_started {tool: "agent_wait"}` until
     its `tool_result`);
  2. a live subagent has `background: false` — `SubagentManager.run_tool` blocks the main turn on a
     foreground `agent` call until it returns;
  3. no turn of its own is running (`agent_finished` has landed) while background subagents go on.
  A turn that started background subagents and **kept working** is not waiting and says nothing;
  that was the one case worth being strict about, or the line would be up for most of every turn.
- **The dots are the animation, and the line never moves.** Four phases — no dots, `.`, `. .`,
  `. . .` — at 600 ms, so a cycle is a little over two seconds: "gently", as asked. Each phase is
  padded back out to the same width, so nothing beside the text shifts as the dots grow.
- **Reduce motion: the desktop's cursor flash time.** Relay has no animation setting of its own, and
  Qt exposes no reduce-motion hint. It does expose the desktop's "do not blink the caret"
  (`QApplication::cursorFlashTime() == 0`), which `RichEditor::setCaretColor` already uses to decide
  whether Relay's own caret blinks. The same signal is used here: at 0 the line is drawn once with
  its dots in full and no timer is created. If the owner wants a Relay-level "Reduce motion" setting
  instead, that is a product decision and both this and the caret should move behind it together.
- **The timer only runs while the line is on screen.** It is stopped when nothing is waiting, when
  anything is typed in the box, when an AI ghost suggestion has taken the placeholder's place, and
  when the desktop says not to blink. `updateGhost()` (which already runs on every keystroke and
  every ghost change) calls the refresh, so the line comes back the moment the box is empty again.
- **No shortcut, so no shortcut hint.** Nothing here is a fast path; it is state.

## Tasks

- [x] `SubagentModel::waitingLine` / `waitingLines` / `hasLiveForeground` — the rule and the wording,
      pure and unit-tested (`src/SubagentsPanel.{h,cpp}`).
- [x] `Pane::refreshSubagentWait` and the `m_waitCall` / `m_waitDots` state; the `agent_wait` hooks in
      `tool_started` / `tool_result`; the turn clock's wording; the `updateGhost` and
      `SubagentModel::onChanged` refreshes (`src/Pane.h`).
- [x] Tests: `subagents_test.cpp::waitingForSubagentsLine`,
      `subagents_test.cpp::foregroundSubagentsAreTrackedForTheWaitLine`.
- [x] Docs: `docs/ARCHITECTURE.md` (the turn-clock paragraph), `docs/AGENT-SESSIONS-PROTOCOL.md`
      section 8 (how the GUI derives "blocked", since no event was added).

## Where it is

- `src/SubagentsPanel.h` / `src/SubagentsPanel.cpp` — `hasLiveForeground()`, `waitingLine()`,
  `waitingLines()`.
- `src/Pane.h` — `refreshSubagentWait()` (beside `setupSubagentsUi`), `m_waitCall` / `m_waitDots` /
  `m_waitPhase` / `m_waitShown`, `tickTurnClock()`, `startTurnClock()`, `stopTurnClock()`,
  `updateGhost()`, the `tool_started` and `tool_result` handlers.
- `tests/subagents_test.cpp` — the two new cases.

## Evidence

`docs/qa_evidence/2026-09-18-waiting-for-subagents/` — `drive.sh` runs a fresh, isolated Relay under
Xvfb (its own `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_STATE_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, `RELAY_KEYRING=off`) against `fake-provider.py`, and takes seven
screenshots:

| Shot | What it shows |
|---|---|
| 01 | blocked on `agent_wait`: the prompt box says "waiting for 2 subagents . . .", the strip clock "waiting for 2 subagents · 15 s · step 3/256 · Esc stops" |
| 02 | ~1.4 s later: "waiting for 2 subagents . ." — the same line, the dots at another phase |
| 03 | a steer typed over it: only the typed text, nothing behind it |
| 04 | the steer deleted: the line is back |
| 05 | both subagents finished and the turn answered: the ordinary "Shell commands or agent prompts…" |
| 06 | the other way in — the turn ended and one background subagent is still running: "waiting for 1 subagent ." (singular, and no turn clock, because no turn runs) |
| 07 | that subagent finished too: the ordinary placeholder |

`requests.jsonl` is what the fake model received (`agent`, `agent`, `agent_wait`, then the answer;
then `agent` and an answer that ends the turn).

Note for QA: on 2026-09-18 the live run used `RELAY_DATA_DIR` pointing at a copy of `backend/` whose
keybinding id pattern accepted `_`, because another session's uncommitted `ssh.split_same_host`
action in `src/Keymap.h` made every `configure` fail with "Invalid action id". Once that is settled,
run `drive.sh` without it.

## QA checklist

- [ ] `ctest --test-dir build -R subagents` passes, including `waitingForSubagentsLine` and
      `foregroundSubagentsAreTrackedForTheWaitLine`.
- [ ] A turn that calls `agent_wait` shows "waiting for N subagents . . ." in the prompt box, with
      the dots growing about twice a second and the line never shifting sideways.
- [ ] The strip clock says "waiting for N subagents · <s> s · Esc stops" while blocked, and goes back
      to "thinking · …" when the wait returns and the agent works again.
- [ ] Typing removes the line at the first character and the text is never drawn over; deleting the
      text brings it back.
- [ ] A turn that starts a background subagent and goes on working shows nothing; when that turn
      ends with the subagent still running, the line appears with no turn clock beside it.
- [ ] One subagent reads "waiting for 1 subagent", not "1 subagents".
- [ ] The line clears the instant the last subagent ends, and on a new conversation / worker restart.
- [ ] A narrow pane shows a shorter form ("waiting for 3 . . .", then "3 . . .") rather than wrapping.
- [ ] With the desktop's cursor blink turned off (`gsettings set org.gnome.desktop.interface
      cursor-blink false`, or a cursor flash time of 0), the line is drawn once with all three dots
      and never animates.
- [ ] An AI suggestion in an empty prompt box still takes the placeholder's place, and the waiting
      line comes back when the suggestion goes.
