---
id: W8PC
type: work
status: discussing
labels: [bug, theme]
component: [theme]
workstream: terminal
assignee: owner
rank: zzzzzzt
created: '2026-09-19'
acceptance: the sequence that puts one theme's palette on another theme's ground is known, or the report is closed as understood (scrollback printed under the previous theme)
source: 'owner QA, 2026-09-19: "see the dark text here? its too dark"'
links: {plans: [], commits: [], evidence: [], related: [K3RT], github: null}
---
# A pane drew one theme's palette on another theme's ground

## Issue

The owner's screenshot on 2026-09-19 showed a pane whose ground was Dark Copper's charcoal
(`#0e0f12`) while the text on it was IBM Beige's palette: the agent's prose in Beige's ANSI 15
(`#14120d`, a warm near-black, unreadable there), folder names in Beige's ANSI 6 (`#0f5f5a`) and a
muted line in Beige's `text_muted`. Sampled from the image, not inferred. A second report the same
day showed a navy band — Dark Copper's violet blended into charcoal — on a Beige pane.

## What was ruled out

- The engine resolves indexed colours through the palette it holds **at paint time**, proved by
  `ViewTest::anIndexedColourFollowsTheSchemeItIsPaintedUnder`, before and after a scheme switch.
- Switching with `/light` then `/dark` between turns renders correctly (harness:
  `docs/qa_evidence/2026-09-19-echo-band/drive.sh`).
- Quitting and restoring the session between turns renders correctly.
- `LibVtermCore::setColors` writes all 16 palette entries on every `themeChanged()`, and
  `EngineBackend` is connected to that signal.

## Most likely explanation

Scrollback printed under the previous theme in 24-bit RGB, which cannot be recoloured (#K3RT) —
the second report is certainly that. The first is not fully explained by it: those lines were
*indexed*, and indexed text does follow a switch.

## What would settle it

The exact order of the last theme change relative to the output: whether the theme changed between
quitting and the session restore, whether the pane was remote, and which core the pane was on. If
it cannot be reproduced, close it against #K3RT.
