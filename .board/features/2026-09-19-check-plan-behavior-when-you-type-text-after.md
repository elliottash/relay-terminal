---
id: 736Y
type: work
status: planned
rank: zzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
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

Typing `/plan` alone toggles plan mode. Typing `/plan <text>` has a clear, tested outcome and
never silently loses the text; the composer indicates whether it will execute a command or send
a plan-mode prompt.

## Plan

**Goal.** Reproduce and specify the trailing-text behavior, then prevent silent prompt loss.

**Steps.**
1. Add a focused slash-command test for `/plan`, `/plan `, `/plan inspect this`, and a multiline
   prompt starting with `/plan`, asserting both mode and submitted text.
2. If the text is currently swallowed, choose and implement one UX: recommend toggling plan mode
   and submitting the remaining text in that mode. If automatic submission is undesirable, reject
   the arguments visibly and leave the draft intact. The owner should choose between these two.
3. Verify the route preview matches Enter's behavior and run the targeted `slashcommands` and
   `consolemode` tests.

**Risk.** The slash command is also offered in guest panes; establish whether guest `/plan`
passes through to the guest CLI before changing the native path.

**Verify.** Focused tests plus one live composer interaction showing that text after `/plan`
either reaches the plan agent or remains editable with an explicit explanation.
