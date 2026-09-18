---
id: KP4M
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent (jobs)
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, in the shared checkout), 2026-09-18
rank: zzzzzz
created: '2026-09-18'
source: issues/feature_intake.txt, 2026-09-18
acceptance: '`tests/panestatus_test.cpp` (ctest `panestatus`), `tests/subagents_test.cpp` (ctest `subagents`), live run in `docs/qa_evidence/2026-09-18-waiting-for-jobs/`'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-waiting-for-jobs/], related: [V7QD], github: null}
---
# "waiting for N jobs . . ." in the prompt box

## Issue
if a session is waiting for background jobs, indicate "waiting for jobs..." similar to "waiting for
subagents...".

## Decisions

Implementer's reading (Claude Opus 5, 2026-09-18), to be confirmed or overruled by the owner:

- **Same surface, same machinery as #V7QD.** The prompt box's own placeholder, the same four dot
  phases at 600 ms padded to a constant width, the same "vanishes the instant you type", the same
  reduce-motion rule (`QApplication::cursorFlashTime() == 0` draws the dots in full and starts no
  timer), and the same words in the strip's turn clock instead of "thinking".
- **One line for both kinds, naming only what is actually waited on.** The mixed wording is
  **"waiting for 2 subagents, 1 job . . ."** — subagents first, then jobs, singular and plural
  correct, one comma-joined list. Two lines, or two chips, would compete for the one place the eye
  already goes. And the list is not "everything running": a turn blocked on `agent_wait` while a
  background job also ticks along says "waiting for 2 subagents", because that is what it is blocked
  on. When the turn has ended, everything still running is named, since the session is then waiting
  on all of it.
- **Waiting for jobs means: running jobs exist, and either a `command_output` call is in flight or
  the turn has ended.** `command_output` is the exact analogue of `agent_wait` — it is the call that
  blocks on a job — and the `jobs` list is what "background job" means to the user, since the pane
  already shows it under the prompt.
- **An ordinary foreground `run_command` is deliberately *not* a wait**, although the brief
  suggested "a foreground run_command still inside its timeout". Every command is inside its timeout
  while it runs, so that rule would put the line up for most of every turn — exactly the noise the
  #V7QD rule was strict about — and a command is not a *job* until the worker hands it back, at
  which point the call has already returned. Nothing in the protocol distinguishes "this call has
  been going long enough to be a wait" from "this call started 20 ms ago", so honouring it would
  need a new worker event. If the owner wants a long-running foreground command to say so too, that
  is the change to make, and it belongs in the worker.
- **The rule moved to `relay::panestatus`** (`src/PaneStatus.{h,cpp}`). It was on `SubagentModel`
  after #V7QD; with jobs in it that reads wrong, and `PaneStatus` is already "what a pane is doing,
  pure rules, no widgets" — it owns `State::Subagents` — and depends on neither model. It takes a
  `Waiting` struct of counts and flags that `Pane` fills from `SubagentModel` and `JobsModel`. The
  #V7QD unit test moved with it into `tests/panestatus_test.cpp`.
- **No shortcut, so no shortcut hint.** Nothing here is a fast path; it is state.

## Tasks

- [x] `relay::panestatus::Waiting`, `waitingSubject`, `isWaiting`, `waitingLine`, `waitingLines` —
      the rule and the wording, pure and unit-tested (`src/PaneStatus.{h,cpp}`).
- [x] Removed `SubagentModel::waitingLine` / `waitingLines`; `hasLiveForeground()` stays, since it is
      about the rows.
- [x] `Pane::waitingFacts()` and `refreshBackgroundWait()` (was `refreshSubagentWait`), the
      `command_output` hooks in `tool_started` / `tool_result` (`m_jobWaitCall`), the
      `JobsModel::onChanged` refresh, the turn clock's wording (`src/Pane.h`).
- [x] Tests: `panestatus_test.cpp::whatThePaneIsWaitingFor` (covers both kinds, the mixed line, the
      phases and the rungs); `subagents_test.cpp::foregroundSubagentsAreTrackedForTheWaitLine` stays.
- [x] Docs: `docs/ARCHITECTURE.md` (the turn-clock paragraph, rewritten for both kinds and the new
      home of the rule), `docs/AGENT-SESSIONS-PROTOCOL.md` ("Commands as jobs", how the GUI derives
      it, and why a foreground `run_command` is not a wait).

## Where it is

- `src/PaneStatus.h` / `src/PaneStatus.cpp` — `Waiting`, `waitingSubject()`, `isWaiting()`,
  `waitingLine()`, `waitingLines()`.
- `src/Pane.h` — `waitingFacts()`, `refreshBackgroundWait()`, `m_waitCall` / `m_jobWaitCall` /
  `m_waitDots` / `m_waitPhase` / `m_waitShown`, `tickTurnClock()`, `setupJobsUi()`, the
  `tool_started` and `tool_result` handlers.
- `src/SubagentsPanel.{h,cpp}` — the old home of the rule; only `hasLiveForeground()` is left.
- `tests/panestatus_test.cpp`, `tests/subagents_test.cpp`.

## Evidence

`docs/qa_evidence/2026-09-18-waiting-for-jobs/` — `drive.sh` runs a fresh, isolated Relay under Xvfb
(its own `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_STATE_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, `RELAY_KEYRING=off`) against `fake-provider.py`, and takes seven
screenshots:

| Shot | What it shows |
|---|---|
| 01 | blocked on `command_output`: the prompt box says "waiting for 1 job . .", the strip clock "waiting for 1 job · 13 s · step 2/256 · Esc stops" |
| 02 | ~0.7 s later: "waiting for 1 job ." — the same line, the dots at another phase |
| 03 | a steer typed over it: only the typed text, nothing behind it |
| 04 | the job came back and the turn answered: the ordinary "Shell commands or agent prompts…" |
| 05 | the other way in — the turn ended and one background job is still running: "waiting for 1 job . ." and no turn clock, because no turn runs |
| 06 | that job finished too: the ordinary placeholder |
| 07 | a subagent and a job at once: "waiting for 1 subagent, 1 job ." — one line, both named, both singular |

`requests.jsonl` is what the fake model received (`run_command` background → `command_output` → the
answer; then `run_command` background and an answer that ends the turn; then `agent` +
`run_command` background and an answer that ends the turn).

Note for QA: on 2026-09-18 the live run used `RELAY_DATA_DIR` pointing at a copy of `backend/` whose
keybinding id pattern accepted `_`, because another session's uncommitted `ssh.split_same_host`
action in `src/Keymap.h` made every `configure` fail with "Invalid action id". Once that is settled,
run `drive.sh` without it.

## QA checklist

- [ ] `ctest --test-dir build -R 'panestatus|subagents|jobs'` passes, including
      `whatThePaneIsWaitingFor`.
- [ ] A turn that calls `command_output` on a running job shows "waiting for 1 job . . ." in the
      prompt box, with the dots moving a step every 0.6 s and the line never shifting sideways.
- [ ] The strip clock says "waiting for N jobs · <s> s · Esc stops" while blocked, and goes back to
      "thinking · …" when the call returns and the agent works again.
- [ ] Typing removes the line at the first character; deleting the text brings it back.
- [ ] A turn that starts a background job and goes on working shows nothing; when that turn ends
      with the job still running, the line appears with no turn clock beside it.
- [ ] One job reads "waiting for 1 job", not "1 jobs"; likewise "1 subagent".
- [ ] A subagent and a job at once read as one line, "waiting for 1 subagent, 1 job . . .", not two
      lines and not two chips.
- [ ] Blocked on `agent_wait` while a background job also runs: the line names the subagents only.
- [ ] An ordinary short `run_command` (`ls`) shows nothing — the line is for background work.
- [ ] The line clears the instant the last job ends or is stopped from the jobs list, and on a new
      conversation / worker restart.
- [ ] A narrow pane shows the shorter rungs ("2 jobs . . .", "2 . . ."; mixed: "2 subagents, 1 job
      . . .", then "waiting . . .") rather than wrapping.
- [ ] With the desktop's cursor blink turned off (`gsettings set org.gnome.desktop.interface
      cursor-blink false`, or a cursor flash time of 0), the line is drawn once with all three dots
      and never animates.
