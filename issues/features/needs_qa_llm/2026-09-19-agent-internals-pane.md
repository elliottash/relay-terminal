---
id: QT8C
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 subagent and Claude Fable 5.1 fork (Claude Code session), 2026-09-19
rank: zzzzzzz
created: '2026-09-19'
acceptance: an agent internals pane opens beside a terminal pane as an ordinary splitter leaf, streams that pane's reasoning and tool calls live and in order across turns, the terminal prints neither while it is open, and when it is closed the rows it hid are reprinted into the terminal in order and later ones print inline again, and it has a keymap action, a palette entry and a shortcut hint
source: 'issues/feature_intake.txt, 2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-agent-internals-pane/], related: [T8CN, K48R, BDXG, 9K5H], github: null}
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
  next reasoning block and the next call appear inline exactly as they do today.
- **The hidden rows are reprinted on close** (owner, same day, correcting the first draft of this
  card, which had left them out: "i did mean that the hidden rows should be reprinted on close").
  Everything the pane took while it was open goes into the terminal when it closes: each
  "▸ ✦ thought for N s" row and each tool-call row, settled and collapsed, in the order they
  happened, with working fold anchors — a click unfolds a reprinted row exactly like one that was
  printed live (the thinking folds answer from `m_turnThinking`, the call folds through
  `tool_output_get`, as restored rows already do, #EC58). What follows from the grid being
  append-only, and is decided here rather than left to the implementer:
  - The rows land at the cursor at the moment of closing, so they sit *below* answers that printed
    while the pane was open, not above them. They are therefore grouped per turn under a muted rule
    carrying that turn's request (its first line), so the block reads as "what the agent did for
    this", not as new activity. Runs that merged in the pane ("read 6 files") reprint as the one
    merged row.
  - A turn still running at close: its rows so far are reprinted first, then the live rows continue
    under them with no rule in between — that turn reads exactly as if the pane had never been open.
  - Relay only writes into the grid when it may (`inlineReady()`): if a program owns the screen at
    close, the reprint waits until the pane is next ready, and is dropped for nothing.
  - The pane keeps the hidden rows per open period and hands them over once; opening and closing
    again does not print them twice. A pane closed because its owner is closing reprints nothing.
  - Bound: the worker keeps detail for the last 50 turns, so at most the last 50 hidden turns are
    reprinted; older ones collapse to a single muted "… N earlier turns" row.
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

## As built (2026-09-19)

- `src/AgentInternalsView.{h,cpp}` — the view (`relay::AgentInternalsView`, a `relay::PaneView`
  hosted by `ToolPane(Kind::Internals)`, pane type `internals`): rule per turn, reasoning blocks
  rewritten in place while they stream, one row per tool call (running → settled, runs merged),
  folds through `tool_output_get` with `int-` ids, big diffs to the diff pane, pinned to the bottom
  with End re-pinning. `tests/agentinternals_test.cpp` (11 tests).
- `src/InternalsLedger.{h,cpp}` — what the pane took, per turn, for the reprint: order, the run
  rewrite rule, hand over once, the 50-turn bound with the dropped count.
  `tests/internalsledger_test.cpp` (8 tests).
- `src/Pane.h` — `attachInternals` / `detachInternals` / `openInternalsPane`, the event routing
  (`thinking_delta`, `thinking_done`, `tool_started`, `tool_output`, `tool_result` go to the view
  and the ledger while a pane is attached), `reprintHiddenRows()` and `printAnchoredRow()`, the
  `int-` reply and error paths, the reasoning fold's "open in pane" now `relay://internals/…`,
  the mid-block hand-over (`finishThinkingFold` takes the settled label), the reprint deferred
  through `flushInline()` while a program owns the screen.
- `src/RelayWindow.h` — `openInternalsPane` / `createInternalsPane` / `linkInternalsPane` /
  `linkRestoredInternalsPane`, the `agent.internalsPane` dispatch, the palette row, the
  `{"internals": {cwd, owner}}` layout node. `src/PaneChrome.h` — `Kind::Internals` and its node;
  `src/PaneStatus.cpp` — the pane type's band label; `src/Keymap.h` — **Alt+Shift+R** (Alt+R's
  shifted neighbour; no preset binds it).
- Shortcut hint `internals.open` on the link and palette paths ("Next time: Alt+Shift+R …").
- Decisions taken here, inside the card's letter: the pane's header line says what closing it will
  do to the terminal; a block that streamed before the pane existed is seeded into the view when
  the fold's link opens it, under "✦ reasoning so far in this turn", so the link always lands on
  something; the in-progress block at close time is not in the ledger (its row is written when
  the block ends), so closing mid-reasoning reprints the rows before it and the fresh inline fold
  then shows the whole block; `agent/thinking_display=never` is said once in the pane.

## Not in this card

- The Switchboard card conversation showing thinking (#9K5H) — same view, different host; do that
  there once this view exists.
- The phone / browser remote view.

## QA checklist

- [ ] Open the pane, run a turn with reasoning and several tool calls: all of it streams in the
      pane, none of it in the terminal; the answer prints in the terminal as usual.
- [ ] Close the pane after two hidden turns: both turns' thought and tool rows are reprinted, grouped
      per turn, in order, and each reprinted row unfolds on a click.
- [ ] Close the pane mid-turn: that turn's rows so far are reprinted, then the next tool call prints
      inline under them.
- [ ] Close it while a full-screen program owns the screen: the reprint arrives once the prompt is back.
- [ ] Open and close again with nothing new: nothing is printed twice.
- [ ] Open it mid-reasoning: the inline fold settles, the pane shows the block from its start.
- [ ] Drag the pane to another split position; close its owner — the internals pane goes with it.
- [ ] Two terminal panes in one tab each get their own internals pane.
- [ ] The hint appears on a mouse/palette open and respects the hint limit.
