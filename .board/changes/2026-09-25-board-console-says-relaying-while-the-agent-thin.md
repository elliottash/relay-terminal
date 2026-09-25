---
id: 6YS5
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: kimi/k3
session: 9c518869-5cd5-4b08-8e2a-f8c0ec717009
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
links: {plans: [], commits: [95d53c84f7cf, 94d21ee0fd3a], evidence: [docs/qa_evidence/2026-09-25-boardturnsurface/], related: [], github: null}
---
# Board console says "Relaying" while the agent thinks on another card

## Issue
A board console keeps showing the "Relaying · …" busy line for a card turn that no longer belongs to the surface it is drawing: when the card page's one console is repointed to another card mid-turn (card #CTRN), the turn's agent_finished is routed to the departed surface and never reaches it, so the busy line — and everything m_agentBusy gates — stays stuck on a card whose agent is idle.

> bug: switchboard agents shouldnt say relaying if the agent is thinking on ano ther card
> — elliott · [session:c1facb76ed574045a4ed7b5f45713f08](relay://session/c1facb76ed574045a4ed7b5f45713f08) · 2026-09-25

## Done means
- A board card console repointed to another card mid-turn (card #CTRN) no longer shows the departed card's "Relaying · …" line — the busy line belongs to the surface the console is drawing.
- Arriving at a card whose turn is already running shows the busy line again (the strip's facts, restored on open).
- A tab (switchboard-conversation) turn keeps its busy line on every console it prints in, across card switches.
- Proved by a targeted test in tests/boardpane_test.cpp driving the console with surfaced `agent_started`/`agent_finished` around a `clearTranscript` switch.

## Plan
**Goal.** The busy line follows the surface: `Relaying · …` shows on a console only while the turn it belongs to prints there.

**Findings.**
- `Pane::clearTranscript` (src/Pane.h:4651, card #CTRN) banks the transcript on card switch but not `m_agentBusy`/`m_turnText`/the turn clock.
- `deliverToConsoles` (src/RelayWindow.h:6244) routes `agent_finished` by the console's *live* surface (`CardContext::spec()` reads the open card), so the departed card's finish never reaches the repointed console — busy sticks, and gates `Esc`/compaction prompts too.
- Unsurfaced tab turns broadcast to every console by design; their busy must survive a card switch.

**Steps.**
1. Stamp the busy line's owner: `m_busySurface` set from `agent_started`'s `surface` (empty = the tab conversation), cleared with busy.
2. In `clearTranscript`, when busy belongs to a surfaced turn that is not the arriving surface, drop it (busy flag, turn text, clock, current item) — the departed card's strip is the board's own and already correct.
3. Restore on arrival: extend `relay::agent::ConsoleHandle` with `turnRunning(mode)`; BoardPane calls it from the `board_card` open site that already restores `m_detail->setBusy` from `m_cardTurns`.
4. Add a test to tests/boardpane_test.cpp: surfaced `agent_started` → busy; `clearTranscript` to another card → busy gone; clear back / `turnRunning` → busy again.

**Risks.** None for terminal panes (`clearTranscript` is a no-op for them — no `m_transcriptSurface` switch). The restore starts the elapsed clock at switch time; the strip keeps the true mode.

**Verify.** `ctest -R boardpane` plus the new case; build through `land.py try`.

## Execution Summary
Landed in `95d53c84f7cf` (built and gated in the verify slot; other panes' concurrent edits to the same files were left uncommitted, including #05J2's two `CMakeLists.txt` hunks and #265N's `remoteshutdown` registration, which would otherwise have ridden along inside shared hunks).

- `agent_started` now stamps the turn's `surface` on the busy line (`m_busySurface`, src/PaneEvents.cpp); `agent_finished` and the `queue_changed` stale-clear clear it. An empty surface is the tab's own conversation, which prints on every console.
- `Pane::clearTranscript` drops the busy line when it belongs to a surfaced turn on the surface being left — the departed card's `agent_finished` is routed to that surface and can never reach the repointed console, so before this the line (and everything `m_agentBusy` gates) stuck.
- `ConsoleHandle::turnRunning` + `Pane::consoleTurnRunning()`: the card page's `board_card` open site — the one that already restores the `Switchboarding · …` strip from `m_cardTurns` — now also restores the console's own `Relaying · …` line for a card that was already working when the console arrived.
- New test file: `tests/boardturnsurface_test.cpp` (ctest `boardturnsurface`, registered beside `consolemode`'s own executable, not in `boardpane_test.cpp` as the Plan first sketched — that file is held by several sessions and CMakeLists' convention says new tests get their own target). Three cases: drop on move-off (plus tolerance of a late stray finish), restore on arrival and clear by its own finish, and a tab turn keeping its line across switches.

## Tests
- `boardturnsurface` (new, tests/boardturnsurface_test.cpp) — **passed** on the exact landing tree in the land verify slot (`land.py commit --verify-tests boardturnsurface`, slot relay-terminal-1006c7a3-0): 3/3 cases, 0.66 s.
- `consolemode` — the suite fails on *different* cases on each run (kill-timing case `tests/234z_cases.h:87` in one run; transcript-text cases `consolemode_test.cpp:2004/2007/720/721/733` in another) in the verify slot **and identically in the shared `build/`**, i.e. with and without this change — a pre-existing flake tracked separately (not by this card). The areas this change touches (`--234z-only`) pass in isolation.
