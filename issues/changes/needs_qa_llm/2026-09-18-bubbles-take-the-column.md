---
id: N3WD
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, subagent), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'With the reasoning panel or the queue strip up, the terminal shrinks and its last lines stay visible; the PTY reflows; a terminal at the newest output stays there; a pane too short to draw a bubble legibly shows none; `ctest` passes'
source: 'owner, 2026-09-18: "when there is a thinking bubble, or a queue bubble, the terminal needs to move up, rather than being covered up" and "ok hide it" on the too-short case'
links: {plans: [], commits: [455c475, cefdb02], evidence: ['docs/qa_evidence/2026-09-18-bubbles-take-the-column/'], related: [], github: null}
---
# The thinking and queue bubbles move the terminal up instead of covering it

## Report

Both bubbles were absolutely-positioned overlays over the terminal host, anchored to its bottom,
so they hid the last rows — which is exactly where a shell prints what you were waiting for.

## Change

- **Rows, not overlays.** Both are rows of the pane's own column now, between the terminal and the
  prompt box, in the order they used to stack: reasoning above the queue. The terminal host
  shrinks, the PTY reflows (measured: 23×121 with a bubble up against 27×121 without).
- **Scrollback stays put.** `viewportAtBottom()` reaches the GUI through `TerminalBackend` (added
  to `VTermBackend` and `TerminalView`), is sampled before the resize and restored behind the
  view's own geometry pass, so a terminal showing the newest output still shows it and a reader
  who had scrolled back is left alone.
- **#G152 respected.** Every show, hide and height change runs inside `keepPaneSizes`, and both
  bubbles are horizontally `Ignored` like the composer, so they never widen the pane's minimum or
  make a splitter redistribute its row.
- **Height is a maximum over a one-row floor,** never a fixed height: a fixed one made a pane in a
  three-high stack overflow its own column and draw the queue strip through the prompt box.
- **Too short to read means hidden** (`bubbleRoom`, `bubbleRow`). A bubble needs one legible row —
  a line of its header plus padding — or it is not shown. When only one fits, the queue keeps it:
  it holds work (what is queued, Resume, Clear) while the reasoning is commentary and is
  dismissible by hand. Room is measured from the pane and the rows around it, never from the
  terminal, so the decision cannot oscillate.

## QA checklist

1. **Nothing is covered.** Fill a pane with output, queue a command behind a slow one
   (`sleep 25`, then two more): the queue strip appears as a row and the last output line and the
   shell prompt are both still visible above it.
2. **The PTY reflows.** With a bubble up, `tput lines` reports fewer rows than without.
3. **Scrollback.** At the bottom of the scrollback when a bubble appears, the newest line is still
   shown afterwards. Scrolled back into history, the view does not jump.
4. **Expand and dismiss.** ▴ grows the reasoning panel and the terminal shrinks further; × hides
   it and the terminal grows back.
5. **Too short.** Shrink the window until a pane is a few rows tall: the strip disappears rather
   than drawing a sliced header, and comes back when the pane grows. Check both bubbles up in a
   short pane — the queue is the one that survives.
6. **Siblings.** In a three-pane row and a three-pane stack, dividers must not move when a bubble
   appears or disappears.

## Known gaps

- The compact reasoning panel measured its own chrome wrongly and drew a header over an empty box;
  fixed with the Alt+R work (card R8QM), not here.
- A pane between about 280 and 320 px tall draws the strip with its last row clipped at the very
  bottom edge; it is legible, but the floor could be a row taller.
