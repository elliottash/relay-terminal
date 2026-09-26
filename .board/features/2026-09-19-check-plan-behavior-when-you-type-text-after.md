---
id: 736Y
type: work
status: executing
assignee: codex
verify: {artifact: code, primary: script, human: none, effort: medium}
rank: zzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [9a57ea6202313f930577231d2786c7b84f699e5b, 2db31afece20989d88842a1cf8a2164217cc1161], evidence: [], related: [], github: null}
---
# check /plan behavior when you type text after.

## Issue
check /plan behavior when you type text after.

## Planning notes

Freshness check, 2026-09-26: `/plan` is in the built-in slash list (`src/Pane.h`), its
dispatcher calls `togglePlanMode()` (`src/Pane.cpp`), and the command recognizer accepts an
argument string. The `/plan` branch does not use that string. A prompt such as `/plan inspect
this` can therefore be consumed as a mode toggle without sending `inspect this` to the agent.
This needs a behavioral test before deciding how to change the UX.

## Done means

Typing `/plan` alone toggles plan mode. Typing `/plan <text>` enters plan mode and submits
`<text>` as the plan-mode prompt, preserving multiline text. The route preview matches Enter,
and guest CLI slash commands retain their own semantics.

## Plan

**Goal.** Reproduce and specify the trailing-text behavior, then prevent silent prompt loss.

**Steps.**
1. Add a focused slash-command test for `/plan`, `/plan `, `/plan inspect this`, and a multiline
   prompt starting with `/plan`, asserting both mode and submitted text.
2. Implement the owner’s chosen behavior: enter plan mode and submit trailing text in that mode.
3. Verify the route preview matches Enter's behavior and run the targeted `slashcommands` and
   `consolemode` tests.

**Risk.** Preserve guest CLI `/plan` pass-through semantics.

**Verify.** Focused tests plus one live composer interaction showing that text after `/plan`
either reaches the plan agent or remains editable with an explicit explanation.

## Decisions

Owner chose YES: `/plan <text>` enters plan mode and submits `<text>` as its prompt; never discard the text.

## Tests

### Check: targeted native slash behavior

Passed `QT_QPA_PLATFORM=offscreen ./build-fast/relay-consolemode-tests --plan-click-only` in queue workspace `wt47351d9b72045be8`: bare `/plan`, trailing whitespace, text submission after mode switch, multiline text, and route preview. Passed `./build-fast/relay-slash-tests` (11 tests). The first publication job `cc97c4eb6e4351df` failed the accepted full gate and reconciliation rejected oversized `Pane.h`. The repaired diff removes `Pane.h` from the change. Resubmission `838f3f6aaaaad61e` failed the accepted full gate with 15/122 unrelated CTest targets failing; reconciliation exceeded its case budget. Neither job has a receipt. Keep this card executing until publication lands.
