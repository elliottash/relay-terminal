---
id: QT8C
type: work
status: ready
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzzzzz
created: '2026-09-19'
acceptance: an agent internals pane opens beside a terminal pane as an ordinary splitter leaf, streams that pane's reasoning and tool calls live and in order across turns, the terminal prints neither while it is open and prints both again once it is closed, and it has a keymap action, a palette entry and a shortcut hint
source: 'issues/feature_intake.txt, 2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [T8CN, K48R, BDXG, 9K5H], github: null}
---
# An agent internals pane: watch the thinking and the tool calls beside the terminal

## Issue
"open in pane" link for thinking bubble doesnt work. but actually lets build that more intentionally. have a separate agent thinking pane (side-by-side but draggable) that you can then watch separate from your terminal pane. we could also discuss, whether tool calls etc should go in this pane when its enabled.

## Decisions (owner, 2026-09-19, planning conversation)

- **What it holds: thinking and tool calls.** Reasoning blocks and tool-call rows, interleaved in the
  order they happened, live. The terminal keeps the answer and the shell.
- **What the terminal shows meanwhile: "nothing at all. but they come back when the agent internals
  pane is closed."** While the pane is open its owner prints no ✦ thinking row, no thinking fold and
  no tool-call rows. When it is closed, the terminal prints them again from that moment on — the
  next reasoning block and the next call appear inline exactly as they do today. Rows that went to
  the pane while it was open are not replayed into the grid (scrollback is append-only); the
  turn's "✦ N tool calls" line still prints at the end of every turn, so each hidden turn keeps one
  line in the terminal that opens the whole of it.
- **The name is "agent internals"** (the owner's words). Keymap action `agent.internalsPane`.
- **A pane, not an overlay** (standing rule): a `ToolPane` leaf inserted beside its owner with
  `insertBeside`, so it drags, splits, resizes and closes like every other pane. One per owner pane;
  opening it again focuses the one that is there. It follows its owner across turns rather than
  being bound to one turn the way the turn pane (`TurnTranscriptView`) is.

## Shape

- New view `src/AgentInternalsView.{h,cpp}`: a scrolling log. A reasoning block is a
  "✦ thinking… / ✦ thought for N s" header over its text rendered as markdown in the muted ink,
  uncapped (this pane is where the whole text lives — the inline fold's 6/18-row caps of #K48R do not
  apply). A tool call is its § 23 label row (running → settled, merged runs as one row), which
  unfolds in the view to the call's detail through the same `tool_output_get` round trip the inline
  folds use; a diff of more than 12 changed lines still goes to the diff pane. A turn boundary is a
  thin rule with the request's first line.
- It stays pinned to the bottom while streaming; scrolling up unpins, End or reaching the bottom
  re-pins.
- `Pane` gets one sink (`m_internals`, a guarded pointer): `thinkingDelta` / `thinking_done` and the
  call-line steps go to the sink when it is set and to the terminal when it is not. Opening the
  pane mid-block closes the inline fold cleanly (anchor row settled to "✦ thinking moved to the
  internals pane") and hands the view the text so far; closing it mid-block starts a fresh inline
  anchor for what follows.
- The thinking fold's "open in pane" link and the thinking-panel shortcut's neighbour open this
  pane, scrolled to that block. The turn pane stays what "✦ N tool calls" opens.
- `agent/thinking_display=never` is honoured: the pane then shows tool calls only and says so once.
- Shortcut hint (standing rule): opening it from the link or the palette shows
  "Next time: <agent.internalsPane key>".
- Session restore: the pane is restored as open beside its owner, empty until the next event.

## Not in this card

- The Switchboard card conversation showing thinking (#9K5H) — same view, different host; do that
  there once this view exists.
- The phone / browser remote view.

## QA checklist

- [ ] Open the pane, run a turn with reasoning and several tool calls: all of it streams in the
      pane, none of it in the terminal; the answer prints in the terminal as usual.
- [ ] Close the pane mid-turn: the next tool call prints inline in the terminal.
- [ ] Open it mid-reasoning: the inline fold settles, the pane shows the block from its start.
- [ ] Drag the pane to another split position; close its owner — the internals pane goes with it.
- [ ] Two terminal panes in one tab each get their own internals pane.
- [ ] The hint appears on a mouse/palette open and respects the hint limit.
