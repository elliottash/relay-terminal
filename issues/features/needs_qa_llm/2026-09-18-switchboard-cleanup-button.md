---
id: KDK9
type: work
status: needs-qa-llm
labels: [feature, switchboard, worker]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, subagent) backend + Claude Opus 5 (Claude Code) GUI, 2026-09-18
rank: 0a
created: '2026-09-18'
acceptance: '`board_cleanup` runs one agent turn over the whole board with a brief, merges and splits cards and edits the board''s sections through three cleanup-only tools, deletes nothing, refuses to run beside a `board_ask`, and ends with a summary event and a dated changelog; a live run on a copy of this repo''s 129 cards is in `docs/qa_evidence/2026-09-18-switchboard-cleanup-button/`'
source: 'owner, 2026-09-18: "there should be a cleanup button, that would have the agent clean up the board, merge / split sections, merge redundant cards, split eclectic cards, review card status, etc."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-switchboard-cleanup-button/'], related: [], github: null}
---
# The Switchboard cleans itself up: `board_cleanup`, its three tools and its changelog

Both halves are here now: the worker's `board_cleanup` turn, and the button in `src/BoardPane.cpp`
that sends it (added 2026-09-18, see **The button** below). The contract between them is
`docs/AGENT-SESSIONS-PROTOCOL.md` section 19.9; the UI is `docs/SWITCHBOARD-DESIGN.md` 4.8.

## Issue

there should be a cleanup button, that would have the agent clean up the board, merge / split
sections, merge redundant cards, split eclectic cards, review card status, etc.

## Change

**One message, the machinery of `board_ask`.** `board_cleanup {id?, scope?, note?, dry_run?,
limits?}` starts one turn on the Switchboard worker — the same worker, supervisor, streaming and
`cancel` a card's ask already uses. The only differences are the ones the GUI needs: its turn
events carry `cleanup: true` and a `run_id` instead of a `card_id`, so the pane draws progress in
the board's notice area rather than in a card thread, and the run ends with
`board_cleanup_summary` before the usual `board_changed`.

**The brief is text, not code.** `backend/relay_core/board_cleanup_brief.md` sits beside
`board_policy.md` and is sent as the turn's prompt, with the board's sections, its tabs, the
per-status counts and one roster line per card. It tells the agent to read before it writes, that
doing less and saying why is a good outcome, that the intake files are the owner's and are never
touched, that it may not run a git command that writes, and it dictates the shape of the closing
report (merged / split / status / labels / sections / **left alone**).

**Three tools it only gets while cleaning** (`CLEANUP_TOOL_SPECS`; outside a cleanup they are
neither advertised nor accepted, so an ordinary pane turn carries neither their schemas nor their
risk):

| Tool | What it does |
|---|---|
| `board_merge_cards {into, cards, reason}` | The survivor gains `## Merged in` (each source's body, headings demoted), the union of the labels and `links.merged_from`; each source keeps its file **and its id**, gains `links.merged_into` and a `## Resolution` linking the survivor, becomes `dropped` and moves to `done/`; its thread is copied over, each entry keeping its text and author and gaining `from=` and `orig=`. |
| `board_split_card {id, parts, reason, close?}` | One card per part, each with the part's verbatim `## Issue`, `parent` and `links.split_from`; the original gains `## Split` and `links.split_into`, and is closed only with `close: true`. |
| `board_sections {columns?, tabs?, reason}` | Rewrites `issues/board.yaml`. Refused when a folder that still holds cards would lose its tab, when a column id is unknown, or when nothing would change. |

The file half is new functions in `backend/relay_core/board.py` (`merge_cards`, `split_card`,
`carry_thread`, `append_body_section`, `demote_headings`, `merged_into`, `render_config`,
`write_config`, `flow_value`); the tools are thin wrappers that add the thread events, the undo
snapshot and the changelog line.

**Nothing is deleted.** There is still no delete tool. A merged card is closed *in place*, so a
`#ID` written in a commit message, a doc or another card keeps resolving — to a card that says
where the work went. Undo restores every file one merge or split touched, not just the card it was
recorded against (`board_undone` gains `also_restored`).

**Reviewable.** A cleanup rewrites many of the owner's files, so every run writes a changelog:
run id, model, outcome, counts, a table of every write (action, card, summary, file), the
refusals, and the agent's closing message. It goes to
`docs/qa_evidence/<date>-switchboard-cleanup/cleanup-<stamp>.md` — the closest fit in
`issues/README.md`'s conventions, since `issues/` itself holds cards and threads and any other
Markdown there is parsed as a card. `dry_run: true` makes every write refuse and be recorded as a
proposal, so the button can offer a preview. **The worker never runs git**: reviewing and
committing a cleanup is the person's job.

**Busy rules.** The Switchboard worker runs one turn at a time, and a cleanup and a card's ask are
*refused* rather than queued behind each other — a cleanup running under an open card conversation
would rewrite the card the user is talking about. The loser gets
`{"code": "board_busy", "agent_busy": true, "cleanup_running", "card_id"}`, checked before
anything is written, so a refused ask no longer leaves its question on the card. `cancel` stops a
cleanup like any turn, and the changelog and summary still report what it managed.

**Limits.** `CLEANUP_LIMITS` (60 creates, 400 writes per turn, 200 creates per hour) instead of a
pane turn's 5/20/30; the message may only lower them.

## The button (GUI half, `BoardView` in `src/BoardPane.cpp`)

**A preview, then Apply — no modal.** A cleanup rewrites many of the owner's files, so
`requestCleanup()` never starts one that writes: the click sends `board_cleanup {dry_run: true}`,
the agent makes the same plan, every write tool refuses, and the result panel's **Apply** is the
only path to a run that writes. A confirm dialog was considered and dropped: a plan the person can
read is a better question than "are you sure", and the owner's standing rule is that a new surface
is a pane or in-pane, never a floating overlay.

**Stop is the same button.** While a run is going `#boardCleanup` reads **Stop** in the warning
colour — the shape `CardDetail`'s Ask/Stop already has — and sends the ordinary `cancel`. The
label swap is a `[running="true"]` property with colour and border only, never a font, so the
button still measures what it paints (`tests/buttonfit_test.cpp`).

**Progress.** A run takes minutes, so its line in the board's notice area stays up instead of
timing out after ten seconds: elapsed, the tool or step or last `board_activity`, a running count
of what it has written or proposed, and "nothing is written" while it is a preview. With a card
open in a stacked pane the line is placed above that card's refusal line, reply box and Ask button
(`CardDetail::controlsHeight()`, the one method added there).

**No card ever sees a cleanup's events.** They carry `cleanup: true` and a `run_id` and no
`card_id`, and `handleCleanupEvent()` takes every one of them — turn events, `board_activity` and
the summary — before `handleEvent`'s card routing runs.

**The result is a panel in the list page** (`#boardCleanupPanel`), under the tools and over the
rows: outcome, whether anything was written, the counts, every change with its `#ID` as a link that
opens the card and its file as a link that opens the file, the refusals, the agent's report, a
**Changelog** button that opens the run's Markdown through `onOpenFile`, **Apply** after a preview
that ran to the end and found something, and **Dismiss**. Every control in it is `NoFocus`, so the
arrows still walk the list.

**Busy, said in words.** An ask pressed on a card while a cleanup runs is stopped in the pane
before it is sent, explained on the card, and the typed message is put back in the reply box; the
line goes when the run does. A `board_busy` from the worker names what is running — "A cleanup is
running on this board." or "The agent is answering on #K7Q2." — in the notice, or on the card when
it was that card's ask, with the unsent question restored.

**No shortcut and no palette entry**, so the WARP.md hint rule has nothing to register here: a
cleanup is a rare, minutes-long, board-wide write, and a key for it would be a way to start one by
accident. If one is added later, the hint goes with it.

## Implementer check

`./scripts/test.sh`: 941 tests, 6 errors, all of them in `tests/test_remote_host.py` from another
session's in-flight remote work (secret input, nonces, `remote/audit.py`) and present before this
change. 34 new tests across `tests/test_board.py`, `tests/test_board_tools.py` and
`tests/test_board_protocol.py`.

**Live run** (evidence folder): preset `glm-coding`, main model glm-5.3, against a scratch **copy**
of this repo's `issues/` tree (129 cards), driven over stdin into `backend/worker.py`. 724 s,
83 tool calls, 19 writes, and the agent's own report is in the changelog:

- 1 merge (`#KH72` → `#4WHD`: the owner's model-roles request and the implementation card it landed
  as; `#KH72`'s file, id and text all survive);
- 9 label fixes (the missing `bug`/`feature` axis), 4 `implemented_by` backfills, 4 inbox → ready
  moves, 1 note, 1 new card;
- **0 splits and 0 section changes**, each with a written reason — the brief's "changing nothing
  here is the usual right answer" held;
- 105 `needs-qa-llm` cards left alone, with the honest reason that a snapshot with no
  `docs/qa_evidence/` tree and one commit cannot confirm a landing;
- it found a real fault in the owner's tree and filed a card instead of fixing it silently: two
  cards carry ids the format forbids (`OT32`, `Z3LP`, since renumbered `WFJM` — O and L are not Crockford base32), so **no
  board tool can address them at all**. `relay-board.py check` reports both; they are the only two
  errors on the board and they are not new.

Two frictions the live run exposed, both fixed here: `board_update_card`'s conflict message did not
say what the current hash *was*, and the model repeated the same wrong hash four times (`#K9SR`'s
label is the one write the run lost); and the run's report ran every between-step remark into one
paragraph, so a blank line is now inserted at each tool call.

## QA checklist

1. **It runs and it streams.** With a Switchboard open, send `board_cleanup`: one
   `board_cleanup_started` with a `run_id`, ordinary turn events carrying `cleanup: true` and that
   `run_id` and **no** `card_id`, a `board_activity` per write, then `board_cleanup_summary` and
   `board_changed`.
2. **Preview first.** `board_cleanup {dry_run: true}` changes no file: every write refuses with
   `board_cleanup_dry_run`, the summary has `counts.writes == 0` and a `proposed` count, and each
   change carries `"proposed": true`.
3. **Merges lose nothing.** After a merge, the merged card's file still exists, still has its id,
   is `dropped` in `done/`, links the survivor, and the survivor holds its text under
   `## Merged in` and its thread entries with `from=`/`orig=`. `relay-board.py check` stays clean.
4. **Splits quote the user.** Each new card's `## Issue` is a verbatim slice of the original's, the
   original names its children, and it is only closed when `close: true`.
5. **Sections are guarded.** `board_sections` refuses to drop a tab whose folder still holds cards,
   refuses an unknown column, and leaves a `board.yaml` the backend can read back.
6. **Undo.** Undoing a merge or a split from the toast puts every file back — the survivor, the
   merged cards in their old folders, and any card a split created.
7. **Busy.** A `board_cleanup` sent while a card's ask runs (and the reverse) answers
   `code: "board_busy"`; the refused ask does **not** appear in the card's thread. `cancel` stops a
   running cleanup and the summary says `outcome: "cancelled"` with what it did.
8. **The changelog.** A run that wrote something leaves one file under
   `docs/qa_evidence/<date>-switchboard-cleanup/`; a run that wrote nothing leaves none and reports
   `changelog: ""`. No git command is run by the worker: `git status` after a cleanup shows the
   changes uncommitted.
9. **The intake files are untouched.** `issues/bug_intake.txt` and `issues/feature_intake.txt` have
   the same bytes after a cleanup as before.
10. **The cleanup tools stay locked away.** Outside a cleanup, an agent calling `board_merge_cards`
    gets `board_refused`, and the tool list a pane agent is offered is still the six of 19.7.

### The button (GUI). Evidence: `NOTES-GUI.md` and the `implementer-gui-*` shots.

11. **The first click is a preview.** Press **Clean up**: the message sent has `dry_run: true`, the
    button becomes **Stop**, and when it ends the panel's heading says *nothing was written*. No
    file under `issues/` has changed (`git status`).
12. **Stop.** Press **Stop** mid-run: the pane sends `cancel`, the run ends `cancelled`, the panel
    says *stopped*, and it offers no **Apply**.
13. **Apply.** After a preview that proposed something, **Apply** sends `board_cleanup` with
    `dry_run: false`; the panel that planned it goes away while the real run goes, and the new
    panel says *the board was rewritten* with the written counts and no **Apply**.
14. **Progress.** While a run goes, the notice area shows the elapsed time, the step or the last
    write, and the count — and it does **not** disappear after ten seconds the way a move's notice
    does. With a card open in a narrow pane it sits clear of the reply box and the Ask button.
15. **Nothing lands in a card thread.** Open a card, start a cleanup, let it write: the card's
    thread gains the ordinary per-write events (19.5) and **nothing else** — no `delta`, no tool
    line, no report.
16. **Busy, both ways.** With a cleanup running, "Ask the agent" on a card is refused in the pane
    with a sentence naming the cleanup, and the question stays in the reply box rather than going
    to the thread. With an ask running, **Clean up** comes back from `board_busy` saying the agent
    is answering on that card, and the button goes back to **Clean up**. When the cleanup ends, the
    card stops saying a cleanup is running.
17. **The panel's links.** A `#ID` in the result opens that card; a file name opens that file;
    **Changelog** opens the run's Markdown in a pane. **Dismiss** puts the panel away, and at no
    point does the panel take the arrow keys off the list.
18. **Labels fit.** **Clean up**, **Stop**, **Apply**, **Changelog** and **Dismiss** all paint in
    full, at a wide pane and at ~400 px.

## Known gaps

- **`scope` and `note` have no UI.** The message carries them and the worker passes them to the
  model, but the button sends neither: there is nowhere to type "only the Needs QA lane" yet, so
  every run from the pane is the whole board.
- **Apply was not exercised live** by the implementer — only what it sends is tested — because a
  real applying run on the scratch board was more provider time than the check needed.
- **A run that ends without a summary** (a worker that dies mid-turn) is caught by a 20-second
  guard that ends the run in the pane and says so, rather than leaving the button on **Stop**.
- **Card ids the format forbids** (`OT32`, `Z3LP` on this board at the time of the run; `Z3LP` has since been renumbered `WFJM`) are invisible to every board
  tool, so a cleanup cannot fix them. The live run filed a card about it; it is not fixed here.
- A merge's undo is best-effort for a card a split created that git has already seen: it is left in
  place rather than removed, exactly as a single creation's undo is.
- `scope` and `note` are passed to the model as words, not enforced: a run told "only the Needs QA
  lane" can still touch another lane, and only the changelog would show it.
- The summary's `changes` list is capped at 200 entries; the changelog file is not.
