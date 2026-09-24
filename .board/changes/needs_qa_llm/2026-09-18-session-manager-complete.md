---
id: SM4R
type: work
status: needs-qa-llm
labels: [change, feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 with two Opus subagents (Claude Code, session relay-terminal-d4), 2026-09-18
rank: zzsm
created: '2026-09-18'
acceptance: '/resume, /sessions and /conversations open one pane whose rows carry a summary and tags; the box understands operators and shows them as chips; → unfolds a quick look; sorts, filters, group by date and a Continue section work; a session can be summarised on demand and in a batch that asks first; ctest and the Python suite pass'
source: 'owner, 2026-09-18: "improve the /resume feature. it will list the history of sessions in a perfect / smooth / useful / powerful way. those need to be full text search … brainstorm useful ways to filter and sort to make it easy to find the convo you want. include an agent summary. make it easy to uncollapse a small preview." and "make /sessions also go to /resume. keep going until the planned design is complete"'
links: {plans: [], commits: [b2c3474, 5dfac36], evidence: ['docs/qa_evidence/2026-09-18-session-manager-complete/'], related: [R6J0, CCKY, RC7Z, JRWQ], github: null}
---
# The session manager, completed: operators, summaries, quick look, Continue

## Issue

The owner's brief, verbatim above. Card #R6J0 made the session manager one pane; this card is the
rest of the planned design on top of it.

## Change

**Worker (b2c3474).** `conv_index.py` schema v3: the box understands `project: file: model: branch:
before:/after: has:tasks|edits|summary is:pinned|unfinished in:terminal|agent` and `-word` /
`-"phrase"`, all bound parameters; new per-session columns (summary, first/last prompt, files
written, has_edits, branch, unfinished, mode) with filters to match; title and summary are
searchable and outrank body text in "relevance"; `conversation_get` returns an `overview`; the
search reply carries `parsed` (for chips) and `facets` (for the menus). `titles.py` writes a two-
or three-sentence summary on the chores (Lite) call at the title's cadence; `conversation_summarize`
does one saved session (into its meta file), `conversations_summarize_estimate` / `_all` / `_cancel`
do a batch that never starts on its own. Protocol §14 and §18.4.

**GUI (this commit).** `src/Conversations.{h,cpp}`:
- Rows: the title on its own line; under it the tags (pinned, open, closed N min ago, unfinished,
  edits · N files, the branch off main) and the summary, else the first prompt.
- → / Space / the arrow: the quick look under the row — summary or a Summarise button, First:, the
  last turns, Files (N) + "and N more", open todos — fetched once and kept; a refresh keeps the
  selection, the scroll position and what was unfolded, and does not ask the worker again.
- Chips under the box for every operator (`not: pelican` for an exclusion), × takes it out of the
  text; an "ignored:" note for values the worker could not use; "?" documents the operators.
- Menus: model and branch from `facets`; More: Has edits, Unfinished, Pinned, Has a summary, Open
  tasks, Summarise all…; Group by project / date / none; sorts Newest, Oldest, Most turns, Best
  match — Best match is chosen by itself while there is a query, until the user picks one by hand.
- "Continue": with an empty query, the pinned, unfinished and recently closed sessions of this
  project head the list (not repeated below when grouped by project).
- Keys: Enter resume here, Shift+Enter new pane, Ctrl+Enter fork (the loaded session; a saved one
  opens in a new pane and the pane says so), Alt+Enter reopen where it was (a closed pane, via the
  recently-closed list), F2 rename, Ctrl+P pin, Delete, Ctrl+F or `/` to the box, typing on the rows
  types in the box, Esc clears the query first and closes second.
- Summaries: a Summarise button in the preview header and in the quick look; Summarise all… shows
  the estimate inline with Start / Cancel, progress in the status line with a Cancel; rows update as
  the events arrive; the loaded session's `session_summary` updates its row live.
- Empty states: nothing at all; nothing matches (Search all projects / Clear filters).
- The owner-only session rows are muted but no longer italic (legibility rules, #N50J).
- `/sessions` opens the same pane as `/resume` (5dfac36, put back here after 3515be4 reverted it).

## QA checklist

1. `/resume`, `/sessions`, `/conversations`, Ctrl+Shift+Y: the same pane, focus in the box.
2. Rows show the summary line and tags; a session open in another pane shows "open" and Enter goes
   to that pane; a session whose pane was closed shows "closed N min ago" and Alt+Enter reopens it.
3. Type `file:conv -pelican index`: two chips; × on one edits the text; the sort reads Best match;
   clear the box: Newest first again. `project:x` flips the scope menu to All projects.
4. → on a row: the quick look; refresh (change a filter and back): still unfolded, no second fetch.
5. Group by date; More › Unfinished; Clear filters from the empty state.
6. Summarise on a row without a summary (needs a chores-capable key): the row gains its line; More ›
   Summarise all…: the estimate sentence, Start runs with progress and Cancel stops it.
7. `ctest` (44) and `./scripts/test.sh` pass.

Done here under Xvfb (evidence): 1–5 and the estimate half of 6. Not done: the Summarise runs of 6,
which need a live provider key in the isolated profile; the flows are covered offscreen by
`tests/conversations_test.cpp` (`summariesFromTheButtonAndTheBatch`).
