---
id: BJJK
type: work
status: needs-verification
labels: [bug, terminal, engine]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: f9aefae5-56c9-4534-9076-e62f84729cbc
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: guest Claude pane f9aefae5, 2026-09-25
links: {plans: [], commits: [519ef299d2be, cf5b3f745068], evidence: [docs/qa_evidence/2026-09-25-BJJK-blank-pane/], related: [MTCS, 69BV, MDKN, 0TJ9], github: null}
---
# A pane whose conversation text was replayed twice draws blank: one prose block anchored in two places overlaps every block between

## Issue
Pane "Recover research subagents status" (e96b0235, reopened from Sessions as c08c8613) draws nothing at all, not even its prompt, although its terminal buffer holds ~490 lines of the conversation. The buffer contains the conversation's prose rows twice (af0737e5/1, /2, /13, /16, /23, /26 each anchor two separate row ranges, e.g. /2 at rows 31–68 and 394–401). TerminalView::resolveFolds unions every piece of a prose URI into one [min,max] range (the #MDKN rule for markdown-label pieces), so /2 spans 31–401 and overlaps every other block in between; FoldLayer::rebuildAnchors assumes "blocks never overlap", and the layout collapses to zero visible rows.

> this pane is blank for some reason: e96b0235
> — elliott · [session:813c6ae99d924e849354bc405ff21384](relay://session/813c6ae99d924e849354bc405ff21384) · 2026-09-25

## Findings
- Reproduced offscreen with the engine library alone (`/tmp/blankrepro/repro.cpp`, input `~/.local/share/relay/state/scrollback/c08c8613-….txt`): with the saved prose records registered, 0 ink pixels and 0 visible rows at 60/76/79 columns; without them, the same rows draw normally at every width.
- The saved per-session text for the same conversation, with each block anchored once, draws fine: the duplication is the trigger.
- Worker and GUI logs show no error; the pane's bash is idle at its prompt.

## Plan
1. `engine/view/TerminalView.cpp` resolveFolds: when a piece of a prose URI arrives after another block's rows have appeared since that URI's last piece, it is a second copy — restart that block's range from the new piece (newest copy wins) instead of unioning across the gap. Consecutive pieces (markdown-label fragments, #MDKN) still union.
2. Regression in `engine/tests/ViewTest.cpp`: two blocks each printed twice, interleaved; after setProseBlock and a resize the view must still show rows and the prompt.
3. Find why a restart plus reopen replays the conversation's text into a pane that already holds it (#69BV set `m_sessionTextMark = 0` on restore; #KDB4 transcript fill), and stop the duplication at the source.

## Execution Summary
Two causes, both fixed.

1. **The blank view** (`519ef299`, `engine/view/TerminalView.cpp` resolveFolds). A prose block whose rows appear twice with other blocks between the copies was unioned into one range spanning every block in between; the fold layer's "blocks never overlap" broke and no row drew. A piece of a block that arrives after another prose block's piece now starts that block over: the newest copy re-wraps, the older copy stays as the rows it was printed as. Consecutive pieces (#MDKN label fragments) still union.
2. **The doubled text** (`cf5b3f74`, `engine/backend/VTermBackend.cpp` formattedScreenText). The pane saves history + screen, and the screen half read the core's *viewport*: a pane scrolled up at save time saved that stretch of history twice and lost its live screen, and every restart and reopen replayed the doubled rows. It now reads the live screen (as both cores' plain `screenText()` already do) and restores the viewport.

Owner's conversation recovered by hand before the fix: its sidecar (`068046613259444ba9b13f19ad048b93.scrollback.txt`) was rebuilt with one copy of each block and no prose anchors; the file it replaced is at `/tmp/blankrepro/session-sidecar.backup.txt`. Panes whose saved files already hold doubled rows keep them as printed rows; they no longer blank.

## Done means
- A pane whose saved text holds a prose block twice draws its rows and prompt at every width, and re-wraps the newest copy.
- Saving a pane that is scrolled up writes each history row once and includes the live screen, and leaves the view where it was.

## Tests
- `RELAY_ENGINE_TEST=ViewTest relay-engine-tests proseBlockPrintedTwiceStillDraws` — fails without the fix (every visible row empty), passes with it.
- `RELAY_ENGINE_TEST=ViewTest relay-engine-tests savedScreenIsTheLiveScreenWhenScrolledUp` — fails without the fix (each line saved twice), passes with it.
- Whole ViewTest: 80 passed, 0 failed (libvterm; the ghostty core is not built on this machine).
- Real data: the owner's pane buffer through `docs/qa_evidence/2026-09-25-BJJK-blank-pane/repro.cpp` draws at 29–100 columns with the prompt (0 px at 60/76 before).

## Not verified here
- The ghostty core: not built on this machine, so both new tests ran on libvterm only. The fix is in the view and backend layers above the core, and both cores implement the calls used (`scrollViewportToBottom`, `scrollViewportToRow`, `viewportTop`).
