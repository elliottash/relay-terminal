---
id: XPEB
type: work
status: done
labels: [feature, keyboard, agent]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
verified_by: anthropic/claude-opus-5-5 via claude-code
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-visual], human: optional, criteria: engine ViewTest anchor-walk cases pass; live shots show each key doing what the toast says, sign_off: none, effort: low}
source: Claude Code guest pane, 2026-09-25
links: {plans: [], commits: [a71ca0d55115, e3d85ba7649e], evidence: [docs/qa_evidence/2026-09-25-ctrl-j-fold-walk/], related: [], github: null}
---
# Ctrl+J: step through tool calls and reasoning folds from the keyboard

## Issue
Add a keyboard walk over the ▸ fold lines in an agent pane's output (tool calls, reasoning, ✦ turn lines), modelled on the Ctrl+L link walk: Ctrl+J from the prompt box starts at the newest fold, Ctrl+J/Up steps older, Down newer, Enter/Space toggles, Right unfolds, Left folds, Esc leaves. Also fixes the fold hint that advertises an unbound Ctrl+Shift+Return.

> suggest a way to move up and uncollapse tool calls with the keyboard. it could be similar to ctrl+l for enabling toggling through the links. we need somthing like that to toggle through tool calls / thoughts.
>
> lets do ctrl J
> — elliott · [session:8cd2f949da6942868c2bda3cafe7221e](relay://session/8cd2f949da6942868c2bda3cafe7221e) · 2026-09-25

## Done means
- Ctrl+J from the prompt box starts a walk on the newest ▸ tool-call, reasoning or ✦ turn line; Up (or Ctrl+J) goes older, Down newer.
- Enter toggles the fold in place and keeps walking; Right only unfolds, Left only folds; Shift+Enter applies to every fold of that turn; a non-fold line opens as its click would.
- The walked line stays on screen when folds above or around it open, including a fold whose detail arrives later from the worker.
- Esc leaves (and Esc also leaves the Ctrl+L link walk, which it did not on an empty prompt box).
- Ctrl+J in the terminal stays the shell's line feed; clicking a ▸ line teaches Ctrl+J; the palette lists the action.

## Execution Summary
Landed in a71ca0d5.

- **Key:** `folds.step`, Ctrl+J, prompt box only (in the terminal Ctrl+J stays the shell's line feed). Palette entry "Step through tool calls and reasoning", listed in the help's terminal section and in `docs/KEYBINDING-PRESETS.md`. Clicking a ▸ line teaches Ctrl+J.
- **Engine:** `TerminalView::stepAnchor(prefixes, delta, &stop)`, `endAnchorWalk()` and `anchorWalkUris()` walk the OSC 8 runs under `relay://call/`, `relay://open-call/`, `relay://turn/` and `relay://subagent/`. There is one stop per URI; a card row that its `#ID` link splits into two runs counts as one stop. Starting either walk ends the other.
- **Scrolling:** the walk highlight is placed in visual rows while folds are open, lays out a fold whose content has only just arrived, and pins the view so a follow-bottom frame cannot carry the line off the top.
- **Pane:** `stepOutputFold`, `activateOutputFold` (toggle, unfold only, fold only, or the whole turn) and `endOutputFoldWalk`. The walk shares its toast with the link walk, and the pane re-shows the line when a fold's detail arrives from the worker.
- **Esc fix, which also applies to Ctrl+L:** the pane's app-wide filter runs before the window's, and on an empty prompt box it took Esc for Esc Esc (Rewind), or during a turn to stop the agent. Esc therefore never ended either walk. `handleComposerKey` now leaves Enter, Esc and the arrows to the window while a walk runs.

![Walk on the run row after Up then Right: its detail opened in place and the line stays selected on screen](docs/qa_evidence/2026-09-25-ctrl-j-fold-walk/05-up-right.png)

## Tests
`RELAY_ENGINE_TEST=ViewTest relay-engine-tests theAnchorWalkStepsThroughTheHostsLines theAnchorWalkScrollsPastAnOpenFold keyboardLinkWalk keyboardLinkWalkNewestHistory`: passed, 3 runs
`RELAY_ENGINE_TEST=ViewTest relay-engine-tests`: 85 passed, 2 skipped (built with `land.py try`, only this card's hunks)
manual: docs/qa_evidence/2026-09-25-ctrl-j-fold-walk/ (drive.sh under Xvfb with a stub provider; shots 01–09)
