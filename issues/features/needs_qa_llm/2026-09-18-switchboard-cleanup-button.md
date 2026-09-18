---
id: KDK9
type: work
status: needs-qa-llm
labels: [feature, switchboard, worker]
component: [worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, subagent), 2026-09-18
rank: 0a
created: '2026-09-18'
acceptance: '`board_cleanup` runs one agent turn over the whole board with a brief, merges and splits cards and edits the board''s sections through three cleanup-only tools, deletes nothing, refuses to run beside a `board_ask`, and ends with a summary event and a dated changelog; a live run on a copy of this repo''s 129 cards is in `docs/qa_evidence/2026-09-18-switchboard-cleanup-button/`'
source: 'owner, 2026-09-18: "there should be a cleanup button, that would have the agent clean up the board, merge / split sections, merge redundant cards, split eclectic cards, review card status, etc."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-switchboard-cleanup-button/'], related: [], github: null}
---
# The Switchboard cleans itself up: `board_cleanup`, its three tools and its changelog

This card is the **backend half**. The button that sends the message is a separate piece of work in
`src/BoardPane.cpp`; the contract it codes against is `docs/AGENT-SESSIONS-PROTOCOL.md` section 19.9.

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

## Known gaps

- **No GUI.** The button, the notice-area progress and the summary panel are the other half of this
  work, and with them the shortcut hint the WARP.md rule owes a new fast path.
- **Card ids the format forbids** (`OT32`, `Z3LP` on this board at the time of the run; `Z3LP` has since been renumbered `WFJM`) are invisible to every board
  tool, so a cleanup cannot fix them. The live run filed a card about it; it is not fixed here.
- A merge's undo is best-effort for a card a split created that git has already seen: it is left in
  place rather than removed, exactly as a single creation's undo is.
- `scope` and `note` are passed to the model as words, not enforced: a run told "only the Needs QA
  lane" can still touch another lane, and only the changelog would show it.
- The summary's `changes` list is capped at 200 entries; the changelog file is not.
