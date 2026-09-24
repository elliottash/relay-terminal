---
id: XM0T
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent), 2026-09-18
rank: zzzzj
created: '2026-09-18'
acceptance: each terminal pane's header shows its state as a glyph; each tab shows the most urgent state among its panes; notifications only for needs-you, done-while-away and a failed turn, and only when you were not watching
source: issues/feature_intake.txt, 2026-09-18
links: {plans: [], commits: [ac8582e, f1ff47a], evidence: [docs/qa_evidence/2026-09-18-pane-types-and-status/], related: [SPBN], github: null}
---
# Terminal and agent status icons, with notifications

## Issue
add terminal status icons and notifications (a la warp) ->
terminal idle
terminal running
agent recommends terminal command
agent needs response
agent working
subagents working
agent done
(this is brainstorm)

## Decisions
- **Owner, 2026-09-18 — both places, different roles:** each pane header shows that pane's exact
  state as a glyph; each tab shows one glyph for the most urgent state among its panes, so a
  background tab can say "needs you" without being opened.
- **Owner, 2026-09-18 — notify only for** agent needs a response, agent done while you were away,
  and a failed turn, and only when the window is unfocused or the tab is in the background.
  Everything else is a glyph only. "Recommends a terminal command" is a glyph; if the agent is
  actually blocked waiting on it, that is "needs response".
- **Urgency order**, least first: idle, running, subagents working, agent working, recommends a
  command, done, failed, needs you. Done beats working because it is news you have not seen.
- **Shapes, not colours:** ring, triangle, star with two dots, four-point star, prompt chevron,
  tick, disc with a cross, diamond with `!`. Painted, like the title bar's glyphs.
- **Where each state comes from** (no new worker events): `m_agentBusy`; the subagent model's live
  count; `processBusy()`; the screen prompt, the waiting hint and the password field ("a program
  asks you" is needs-you); a `run_in_terminal` prefill still in the prompt box — recommends, or
  needs-you when the agent's next turn waits on it (`report_back`); and the last finished turn.
- **"Agent needs response" without an ask-the-user tool:** a done turn whose reply ends on a
  question, or that left a command in the box it waits on, is "needs you" (and notified as "Agent
  needs you") until you have looked at the pane.
- **"Seen"** = the pane is the focused pane of the current tab of the active window for 1.5 s, so
  coming back to the window still shows what happened for a moment before it clears.
- **Notifications:** the existing centre and `Pane::notify()` (desktop + taskbar alert only while
  the window is not active, as before; `notifications/desktop` still switches the desktop part off).
  What changed: a failed turn you were watching no longer posts, and a turn that asked you
  something posts "Agent needs you" instead of "Agent finished". No new setting was needed. The
  other existing notices (a long command finished, out of memory, password prompt, a finished
  subagent) are unchanged.

## What landed
- `ac8582e` — the states, the urgency order and the facts they are read from, as rules with tests
  (`src/PaneStatus.{h,cpp}`, `tests/panestatus_test.cpp`).
- `f1ff47a` — the header glyph and the tab icon (`src/PaneChrome.h`, `RelayWindow::refreshPaneStatus()`
  on a 400 ms poll, repainting a tab icon only when it changes), `Pane::statusFacts()` and the
  finished-turn bookkeeping in `src/Pane.h`, the notification changes, and the ARCHITECTURE section.

Evidence: [`docs/qa_evidence/2026-09-18-pane-types-and-status/`](../../../docs/qa_evidence/2026-09-18-pane-types-and-status/)
— `implementer-tabs-*`: background tabs that need you, are done, failed, working, running a
command and in ssh, and the bell's list for them; `implementer-layout-*`: a terminal whose agent
has a subagent running (subagents glyph) beside an ssh terminal (running, remote).

## For QA
- [ ] A fresh terminal shows the idle ring in its header and on its tab
- [ ] `sleep 30`: the triangle; back to the ring when it ends
- [ ] An agent turn: the star while it runs; with Relay focused on that pane, nothing lingers after it ends
- [ ] Start a turn, switch to another tab before it ends: that tab shows the tick; the bell has "Agent finished"; opening the tab clears the tick after a moment
- [ ] Same with a reply that ends on a question: diamond with `!`, "Agent needs you"
- [ ] A turn that fails in a background tab: disc with a cross, "Agent turn failed"; one that fails while you watch it: no notification
- [ ] A background subagent still running after the main turn ended: star with two dots
- [ ] The agent hands a command into the prompt box (run_in_terminal prefill): chevron; with report_back, needs-you
- [ ] `sudo true` waiting for a password, or a `[Y/n]` question: needs-you on the pane and its tab
- [ ] Two panes in one tab in different states: the tab shows the more urgent one
- [ ] Every glyph can be told apart in a greyscale screenshot
- [ ] Relay unfocused when a turn finishes: desktop notification and taskbar alert as before; with Desktop notifications off, only the bell
