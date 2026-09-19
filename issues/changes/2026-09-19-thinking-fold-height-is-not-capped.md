---
id: K48R
type: work
status: ready
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzzzzz
created: '2026-09-19'
acceptance: a streaming thinking fold never shows more than 6 rows of reasoning (the tail being written), a finished one opened by hand never more than 18 followed by "… N more lines · open in pane", on any length of reasoning; a test feeds a several-thousand-line block and asserts both caps
source: 'issues/bug_intake.txt, 2026-09-19: "the thinking bubble height isnt capped, its filling up multiple terminal pages."'
links: {plans: [], commits: [], evidence: [], related: [T8CN, QT8C], github: null}
---
# The thinking fold's height is not capped

## Issue
the thinking bubble height isnt capped, its filling up multiple terminal pages.

## Cause

`Pane::thinkingFoldLines()` sets `options.maxLines = 400` (`src/Pane.h`), for the streaming tail and
for the settled fold alike. Four hundred rows is several screens. #T8CN recorded Warp's numbers —
a clipped view of 120 px while streaming, 360 px when done — and they never made it into the fold.

## Decisions

- **Owner, 2026-09-19: Warp's sizes.** About 6 rows while streaming, about 18 when done. A grid fold
  has no scroll view of its own, so the two caps are:
  - *streaming*: the **last** 6 rendered rows — the end is the part being written — under a muted
    "… N earlier lines" row once there is more;
  - *done, opened by hand* (click, the thinking-panel shortcut, or `agent/thinking_display=always`):
    the **first** 18 rows, then "… N more lines · open in pane".
- The caps count rendered rows at the pane's current width, after markdown and wrapping, not source
  lines: one long paragraph must not escape the cap by being a single line.
- "open in pane" keeps pointing at the whole text. Until the agent internals pane (#QT8C) lands
  that is the turn pane; the inbox also says that link "doesnt work" today, so reproducing and
  fixing the click is part of this card.

## QA checklist

- [ ] A long reasoning stream: the fold stays 6 rows tall (plus the "earlier lines" row) throughout.
- [ ] Opened after done: 18 rows and the "more lines · open in pane" row.
- [ ] Narrow pane (40 columns), one 5,000-character paragraph: still capped.
- [ ] "open in pane" on a thinking fold opens the full text.
