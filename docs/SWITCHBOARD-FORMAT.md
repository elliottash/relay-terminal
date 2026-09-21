# Switchboard file format (reference)

The Switchboard **is** a folder in the project: plain Markdown in git that works without Relay, on
GitHub and in any editor. This is the normative reference for the bytes; the product design is in
[`SWITCHBOARD-DESIGN.md`](SWITCHBOARD-DESIGN.md) and [`TASKS-AND-MEMORY-DESIGN.md`](TASKS-AND-MEMORY-DESIGN.md).

**The folder is `.switchboard/`** on a board created from 2026-09-19 on — hidden, so the cards do
not clutter the project's root listing — `switchboard/` on one created between 2026-09-18 and
then, and `issues/` on one that existed before either, including this repository's own, which
stays `issues/` and is never converted. All three names are in `board.BOARD_FOLDERS`
(`relay::projects::boardFolders()` on the GUI side), newest first, and every lookup walks that one
list in that one order, so `.switchboard/board.yaml` wins in a project that somehow has more than
one. Nothing moves a board that already exists — the only thing that renames one is the explicit
"Hide this board's folder" / "Show this board's folder" action (protocol 19.17). The trees below
are written with `issues/` because that is the one in this repository; read the first path element
as *the board folder*, whichever of the three a project has.

**A hidden folder is invisible to a plain `grep`/`rg` over the project**, which is the point — an
agent working the codebase should not turn up a card on every unrelated search — but it also means
an agent reaching for the cards with a bare `rg` finds nothing and may conclude there is no board.
Ripgrep and similar tools skip dotted directories by default; an agent that wants the cards should
use the board tools (`board_read`, `board_card_get`, …), read the generated `BOARD.md` index, or
pass `--hidden` (`rg --hidden` / `grep -r` without ripgrep's default) to see into `.switchboard/`
the way it already sees into `switchboard/` or `issues/`.

Implementation: `backend/relay_core/board.py` (parsing, ids, ranks, task markers, thread appends,
atomic hash-checked writes, the check rules) and `scripts/relay-board.py` (`check`, `index`,
`policy`, `migrate`). Neither calls a model or the network. This document stays the format alone; what reads
and writes it has grown past phase 0 — the agent tools and the pane (`AGENT-SESSIONS-PROTOCOL.md`
§19), the importers ([`PROJECT-INIT-AND-IMPORT.md`](PROJECT-INIT-AND-IMPORT.md)) and the GitHub
sync ([`GITHUB-SYNC.md`](GITHUB-SYNC.md)) — and all of them write exactly these bytes.

## 1. Tree

```text
issues/board.yaml              tabs, columns, autonomy (committed config); the marker that
                               says this project has a Switchboard at all
                               (`switchboard/board.yaml` in a project initialized from
                               2026-09-18 on — same bytes, same rules)
issues/BOARD.md                generated index; never hand-edited
issues/POLICY.md               generated: the Switchboard's rules for an agent that has no
                               `board_*` tools (section 4.1); never hand-edited
issues/<category>/             features/ changes/ design/ marketing/ … = tabs
issues/<category>/<state>/     needs_qa_llm/ needs_qa_human/ needs_review/ needs_labels/
                               needs_ab/ deferred/ done/
issues/memory/                 memory cards (type: memory), retired ones in memory/archive/
issues/aliases/                alias cards (type: alias), retired ones in aliases/archive/
issues/threads/<ID>.md         one append-only thread per card
issues/import-state.json       what an import has already brought in (key → card id), so nothing
                               is imported twice; written by `board_import.apply`, committed
issues/survey-state.json       `{state: pending|running|done}` — whether the Switchboard page
                               agent's opening survey (protocol 19.18) is still owed. Written
                               `pending` by `board_init` when it creates the board, settled
                               `done` when the survey turn ends. A board with no file predates
                               the survey and is never surveyed.
issues/.private/…              the private root: same layout, gitignored (issues/.gitignore)
```

**Folder = tab and coarse lane; front matter = exact state.** The `issue-tracking` skill's "move
the file into the state subfolder" rule still holds, so an agent working without Relay sees the QA
lanes by path alone. `relay-board.py check` fails when the folder and the status disagree.

A card file keeps the name it was created with (`YYYY-MM-DD-slug.md` for work cards,
`<name>.md` for memory cards). Renaming is never required; the id, not the path, is the identity.

## 2. Card file

YAML front matter between `---` fences, then the Markdown body. The body's first `# ` heading is
the title (exactly one copy — it is not repeated in the front matter). `## Issue` holds what the
card is about in the words of whoever asked for it.

```markdown
---
id: K7Q2
type: work
status: in-progress
labels: [voice, mvp]
component: [gui, worker]
milestone: desktop-alpha
assignee: agent
implemented_by: Claude Opus 5 (pane 2)
rank: 0i
created: '2026-09-17'
acceptance: holding Right Alt records speech and inserts the transcript into the composer
source: 'issues/feature_intake.txt, 2026-09-17: "add voice transcribe mode (microphone icon)"'
links: {plans: [], commits: [], evidence: [], related: [M3XJ], github: null}
---
# Voice transcription mode (microphone button, hold Right Alt)

## Issue
add voice transcribe mode (microphone icon). and hold right alt to transcribe. (like warp)

## Tasks
- [x] Add OpenRouter transcription call <!-- t:b7 -->
- [ ] Record audio with QAudioSource <!-- t:a3 s=in-progress -->

## Decisions
- 2026-09-17, owner: "cloud-based, using the existing OpenRouter key."
```

**`## Issue` was `## Request` until 2026-09-18** (owner: "i'm not sure about 'request' there, let's
call it issue"). Both spellings name the same section: readers take either, and a card is settled
on `Issue` the first time something writes that section (`relay_core.board.ISSUE_HEADINGS`). Cards
already filed keep the heading they have — there was no rewriting commit — so a card on disk may
say either, and nothing in `check` cares which.

### 2.1 YAML subset

Only what the format needs, so the backend stays stdlib-only and the bytes stay stable:
`key: scalar`, flow sequences `[a, b]`, flow mappings `{k: v}` (either may wrap across lines),
block sequences of scalars, and `#` comments. Scalars a full YAML reader would retype (dates,
times, `true`, `null`, numbers, hex) are written single-quoted, so `created: '2026-09-17'` is a
string everywhere. Front matter values are single-line; prose belongs in the body.

Parsing preserves the front matter bytes: a card that is read and written back without a field
change is byte-identical. The first field change re-emits the whole block in canonical order
(`id, type, status, section, name, description, kind, topic, scope, private, labels, component, milestone,
workstream, assignee, implemented_by, verified_by, session, waiting_on, parent, blocked_by, aliases, paths, pinned,
reviewed, author, supersedes, label_count, label_output, codebook, priority,
rank, created, acceptance, source, links`, then any other key, sorted).

### 2.2 Fields

Every type: `id`, `type`, `status`, `priority`, `rank`, `created`, `labels`, `assignee`,
`private`, `links`, `aliases`, `source`, `blocked_by`, `parent`, `waiting_on`.

`priority` (2026-09-20, #VKFV) is the row's flag in the Switchboard: an integer from −1 to +3.
It is written only when nonzero — a card with no flag carries no `priority` key — and a value
outside the range is clamped on every write.

| Type | Extra fields | `status` | Folder |
|---|---|---|---|
| `work` (default) | `component`, `milestone`, `workstream`, `acceptance`, `implemented_by`, `verified_by`, `session`, `label_count`, `label_output`, `codebook`, `section` | `inbox`, `discussing`, `planning`, `planned`, `ready`, `executing`, `in-progress`, `needs-verification`, `needs-review`, `needs-labels`, `needs-ab`, `needs-qa-llm`, `needs-qa-human`, `deferred`, `done`, `dropped` | `<category>/` plus the state subfolder |
| `memory` | `name`, `description`, `kind`, `topic`, `scope`, `paths`, `pinned`, `supersedes`, `reviewed`, `author` | `active`, `retired` | `memory/`, `memory/archive/` |
| `alias` | `name`, `kind`, `shell` | `active`, `retired` | `aliases/`, `aliases/archive/` |

- `ready` is what the old tracker called `open`: **agreed and not started**. The pane labels the
  section **Ready to start**, because "Ready" on its own was read as "ready to ship" (owner,
  2026-09-19); the status id is unchanged, so no card file or folder moved. Every section's
  one-clause meaning lives in `board::sectionMeaning()` (`src/BoardModel.cpp`) and is what the
  header tooltips and the hide checkboxes show.
- `planning`, `planned`, `executing` and `needs-verification` are the **stage statuses**
  (2026-09-20, #3XZV): a card walks inbox → discussing → planning → planned → executing →
  needs-verification → QA → done, and Relay makes each move itself at the event that earns it —
  the thread's first entry, a Plan turn starting, a Plan turn leaving its `## Plan` on the card,
  Execute being pressed, the executing agent landing the card, and the verifier's verdict.
  Like `inbox`, none of them has a state subfolder: a stage move is a front-matter change, never
  a file move. `ready`, `in-progress` and the waiting statuses stay valid, so a board configured
  before them is untouched.
- `section` (2026-09-20, #3XZV) is the id of the **manual section** a card is parked in: a
  configured column that collects nothing (below). It wins over the card's status for as long
  as that column exists — a parked card stays parked while its stage moves underneath it, and
  its stage shows in the card detail rather than in the list. A drop on a status column is what
  clears it, and `relay-board.py check` warns about a `section:` that names no configured
  column (`dangling_section`) because those cards fall back to their status section.
- `dropped` cards live in `done/` beside `done` ones.
- `implemented_by` and `verified_by` are **signatures, not free text**, and Relay writes them: the
  worker stamps `provider/model` from its own preset and model when a card enters `executing`,
  `in-progress`, `needs-verification` or a
  QA lane, and again when it leaves a QA lane to `done` (card `#T71W`, protocol section 19.15). The
  provider segment is the *model's* vendor, never the aggregator that routed to it, so a card served
  `deepseek/deepseek-v4.1-flash` through OpenRouter reads `deepseek/deepseek-v4.1-flash` and a local
  endpoint reads `local/<model>`. A guest CLI names the model it ran and the harness that ran it,
  `anthropic/claude-opus-5-20260514 via claude-code`, falling back to `anthropic/claude-code` or
  `openai/codex` when the model cannot be seen. Free text in parentheses is allowed and ignored
  (`anthropic/claude-opus-5 (pane 2)`), which is how the hand-typed values written before 2026-09-19
  keep working. The pair is what `relay-board.py verifier <ID>` answers from. `verified_by` is never `relay-free/…`: verifying is
  not available on the free plan (owner, 2026-09-19), and a close signed by it is refused.
- A **self-closed** card (2026-09-20, `#93WR`) is one whose status is `done` and whose `verified_by`
  is non-empty and **equal to its `implemented_by`**: one pane both wrote the card and closed it,
  with no QA lane in between. That is the *medium* tier of `backend/relay_core/board_policy.md` — a
  card too big for one turn but needing no decision, which the agent closes itself — and it is the
  only thing that marks it: no new field and no new type, because the fold is presentation and a
  self-closed card is ordinary work in the done lane. `board_move_card` stamps it whenever the
  **agent's own** tools move a card to `done` from a status before QA (`executing`, `in-progress`
  or any earlier stage), and gives the card an `implemented_by` in the same write when it has none
  — a small card the agent created and closed without ever claiming it. The **owner's** hand-close
  from the Switchboard is never stamped, so it never reads as self-closed: the owner-side tools
  carry `actor: owner` and no model of their own. `dropped` is never stamped either — nothing was
  shipped. Where done cards are listed, Relay folds the self-closed ones into a single
  "N closed by the agent" row.
- `session` (2026-09-20, #R9G7) is the **pane session token of the session that holds the card**:
  the terminal pane doing the work. Only a claim writes it — the Switchboard's **Execute** button
  (`board_claim` with the token of the pane it just opened) or the pane agent's own `board_claim`
  tool — and always from the pane's own `configure {pane_token}`, never from anything a model
  typed: `session` is an immutable field, so a front-matter patch naming one is refused. A card in
  `executing` (or `in-progress`) **with** a `session` is claimed by that pane, and another agent
  reading the card sees it is taken: its `board_claim` is refused with `board_claimed_elsewhere`
  until the user says to take it over. The Switchboard draws the token's first eight characters on
  the card as a link that reveals that pane, and the claim's `progress` entry carries the same
  token in its attributes (`pane_token`, section 3) so the thread links there too. A card that has
  moved on keeps the field as a record of who did the work; it stops meaning "taken" the moment
  the status leaves `executing`/`in-progress`. It is **dropped** on the two events that make it
  meaningless (owner, 2026-09-20): any write that leaves the card `done` or `dropped` — a move, a
  merge's sources, a split that closes the card — drops it in the same write, and the pane closing
  (or moving to another project) drops it from every card it holds in `executing`/`in-progress`,
  leaving the status and `assignee` alone and writing `Released (<first eight>) · <why>` on the
  thread. So a `session` on an open card always names a pane that was live when it was written.
- A **memory** card is one fact per file. `kind` is `convention | fact | lesson | reference |
  preference` (the memory design called this field `type`; it is `kind` here because `type` names
  the card type), `scope` is `project | team | user`, `paths` auto-attaches the body when a matching
  file is read, `pinned` always loads it.
- An **alias** card (issue `#G8DK`, protocol section 20) is one saved command or prompt per file,
  named `<name>.md`. `name` is what you type to run it (`/name`); `kind` is `command | prompt`.
  The runnable text is the first fenced block of a `## Run` section (or the section itself, for a
  prompt), and the parameters are a list in `## Parameters` (`` - `arg` = `default` — note ``).
  They are in the body, not the front matter, because a default may hold any character while a
  front matter scalar is single-line. The same layout serves the global Switchboard
  (`$XDG_CONFIG_HOME/relay/switchboard/aliases/`), where a repository's `issues/` is not the root.
- A **plan** card's body holds the plan prose and a `## Steps` checklist. It may be used on its own;
  `links.cards` and per-step `card=` markers connect it to work cards.
- `check` rejects a field that is not listed for the card's type, so a typo is caught rather than
  silently ignored.

### 2.3 Ids

Four Crockford-base32 characters (`0123456789ABCDEFGHJKMNPQRSTVWXYZ`, no I/L/O/U) with **at least
one letter**, written bare in YAML and referenced as `#K7Q2`. Random, so two branches never
allocate the same number; never all-digit, so GitHub does not autolink them as its own issues.
A duplicate after a merge is an error from `check`; the newer card is re-id'd and keeps the old id
in `aliases`. `migrate` derives ids from a hash of the file path instead, so the migration commit
is reproducible.

### 2.4 Ranks

`rank` is a fractional index: a string of `[0-9a-z]` that never ends in `0`, compared as plain
text. Cards in a column are shown in rank order, ties broken by path. Moving a card rewrites that
one card (`rank_between(before, after)`), never a shared ordered list that every move would
conflict on. There is always room before, after and between two ranks.

### 2.5 Tasks (`## Tasks`, or `## Steps` on a plan)

```markdown
## Tasks
- [x] Add OpenRouter transcription call <!-- t:b7 -->
- [ ] Record audio with QAudioSource <!-- t:a3 s=in-progress -->
  - [ ] Handle device permission errors <!-- t:c1 blocked_by=a3 -->
- [ ] #M3XJ Right-Alt push-to-talk <!-- t:d9 card=M3XJ -->
- [x] ~~Local whisper.cpp fallback~~ <!-- t:e2 s=dropped -->
```

- GitHub renders these as a task list and hides the comments, so the file stays usable and
  tickable without Relay.
- **Item id**: two Crockford-base32 characters, lowercase, unique within the card, referenced as
  `#K7Q2.a3`. A line typed by hand or by an agent without Relay has no marker; it gets one on the
  next Relay write or from `relay-board.py check --fix`.
- **The checkbox is authoritative for open/closed; the marker refines it.** `- [x]` with
  `s=in-progress` is *done* and the marker is rewritten on the next write; `- [ ]` with `s=done` is
  *open* again. This is how a box ticked in GitHub's web UI wins.
- `s=` is omitted when it matches the box (`open` for `[ ]`, `done` for `[x]`). Statuses:
  `open`, `in-progress`, `blocked`, `deferred`, `done`, `dropped`. A dropped item is `[x]` with
  `~~strikethrough~~`.
- `card=M3XJ` means the item is mirrored by a card; `blocked_by=a3,#M3XJ` stores dependencies
  (never the derived "blocked" state). One nesting level: deeper structure means the sub-item
  should be a card, and `check` warns.
- **Writing one.** `board_update_card`'s `tasks` takes `blocked_by` per item: a number is the
  1-based position of another item in the same call — how a fresh list names items that have no
  ids yet — an item id is one already on the card, and `#K7Q2` is a card. `board.set_task_blockers`
  assigns the ids first, then refuses a reference to nothing, a self-reference and a cycle, since
  all three are `check` errors.
- Writing tasks rewrites only the task lines: every other byte of the body, and of the file, is
  preserved.

### 2.6 Tests (`## Tests`)

What proves this card, and what Check found when it last looked (card #7BM4, protocol section
31.5). One test per Markdown list line: an **invocation** — what a person types to run just that
one — with an optional ` — path` tail naming its source.

```markdown
## Tests
- `ctest -R panelayout` — tests/panelayout_test.cpp
- `tests/test_board_chat.py::BoardChatTests::test_steer`
- manual: docs/qa_evidence/2026-09-20-thing/

### Check 2026-09-20 21:04
- failure · ctest:panelayout — `ctest -R panelayout` is not in the project any more
- notice · unittest:tests.test_board_chat.BoardChatTests.test_steer — was edited; its history was reset
```

- Three spellings, and the parser is deliberately forgiving about the bullet, the backticks and
  the dash (`—`, `–`, `--`, `-`): `ctest -R <name>` (a **regex**, as ctest reads it, so one line
  may name several tests), a Python path with optional `::Class::test_method` (the file or the
  class alone is every case under it), and `manual: <path>` for evidence a person recorded by
  hand. A prose line naming no invocation is ignored rather than reported as a broken test.
- **`### Check <YYYY-MM-DD HH:MM>`** is written by the worker, never by hand: one line per
  finding as `- <severity> · <test> — <message>`, or `- no findings`. Severities are GitHub
  Checks' `failure` / `warning` / `notice`. At most one block per card per hour, and a block
  from the same day is replaced rather than stacked on, so a day of checking leaves one current
  answer. The block's own lines are prose about tests and are never read back as tests.
- The section **gates the landing**: a card does not leave `needs-verification` for a QA lane or
  `done` while a test it lists is gone, has never run, or last failed. A card with no `## Tests`
  section at all is not gated — the missing section is a warning on the card, not a reason it can
  never close. A move may carry `override: "<reason>"`, and the reason is quoted into a
  `decision` thread entry.
- `tests` is in `board_tools.AGENT_SECTIONS`, so an agent writes the section without the write
  being recorded as a rewrite of the owner's own words.

### 2.7 Sections: one per stage (2026-09-20, #Z4HR)

A work card's body is **one section per workflow stage**, written as the stage produces it, in
the order the stages happen (`relay_core.board.CARD_SECTIONS`):

| # | Section | Stage | Written by | Holds |
|---|---|---|---|---|
| 1 | `## Issue` | inbox | the owner, or an agent quoting them | the request, verbatim |
| 2 | `## Decisions` | any | an agent, quoting the owner | owner decisions, wherever they happen |
| 3 | `## Discussion points` | discussing | the owner | what they are thinking about or considering |
| 4 | `## Planning notes` | planning | the planner | decision factors, options not taken, the agent's questions with their options, and the owner's answers |
| 5 | `## Plan` | planned | the planner | how it will be done |
| 6 | `## Tasks` | planned → executing | the implementer | the live checklist (section 2.5) |
| 7 | `## Execution Summary` | executing | the implementer | what was built, and links to the outputs |
| 8 | `## Tests` | executing | the implementer | what was automated (section 2.6) |
| 9 | `## QA checklist` | executing, adjusted at verify | implementer, then verifier, then the QA support agent | what a verifier must check by hand |
| 10 | `## Verdict` | needs-qa-* | the verifier | how it was checked, and the result |
| 11 | `## Resolution` | done / dropped | whoever closes it | a time-stamped record of how the card was removed |

- `## Merged in` and `## Split` are written by the merge and split tools. The **thread** is not
  a body section: it is the append-only file under `issues/threads/`, and these sections are
  its digest. A section earns its place by recording what a stage *produced* — a fact that
  stays true; state that must be kept current against the code (profiling status, say) belongs
  on an object whose lifetime matches the code, not on a card.
- **`Verdict` and `Resolution` are different claims.** The QA-close gate takes a verdict only:
  a card dropped because the owner changed their mind has a resolution, and that does not say
  a verifier checked anything.
- **`AGENT_SECTIONS`** (`board_tools.py`) is this set minus `Issue` — everything an agent may
  write without the write being logged as a rewrite of the owner's own words. `Issue` stays
  owner text. Headings outside the set (`Change`, `Implementer check`, the forty other
  spellings the board grew) are owner text too, so converting an old card logs its rewrite in
  the thread.
- **A parenthesized suffix still names its section**: `## Decisions (owner, 2026-09-20)` is
  `Decisions`.
- **`check` warns, never errors**, on a `## ` heading outside the set (`unknown_section`). The
  board predates the schema by hundreds of cards and there is **no bulk migration**: existing
  cards are left alone and converted when a card is next touched, and the warning keeps the
  backlog visible and countable.
- Memory and alias cards keep their own layouts (`## Run`, `## Parameters`) and are not
  checked against this set.

## 3. Threads

One append-only file per card, `issues/threads/<ID>.md` (private cards:
`issues/.private/threads/<ID>.md`), listed in `.gitattributes` as

```gitattributes
issues/threads/*.md merge=union
```

so a union merge keeps both sides' appends instead of conflicting. Entries are self-contained
blocks introduced by an HTML comment, separated by a blank line:

```markdown
<!-- relay:entry 20260917T141203Z-a1 author=owner kind=comment -->
### Elliott Ash · 2026-09-17 10:12
where should transcription run? cheap is fine

<!-- relay:entry 20260917T142010Z-c3 author=agent kind=event model=kimi-k3 turn=s9f2/t-14 -->
- ✦ agent moved Discussing → In progress · assignee agent · pane 2
```

- **Entry id** `YYYYMMDDTHHMMSSZ-xx` (UTC plus two random characters): sortable as text, unique
  after a union merge, stable for linking. Relay sorts on display; `check --fix` re-sorts a file
  that a merge interleaved.
- `kind` is `comment | question | decision | evidence | progress | note | event | task | plan |
  rewrite`. Attributes are `key=value`, quoted with `"` when the value contains spaces.
- `pane_token=<session token>` on a `progress` entry is the terminal pane the card was handed to
  or claimed by (#HKAP, #R9G7): the Switchboard draws such an entry as a link that reveals that
  pane. At most 64 characters, and neither whitespace nor `>` — a value the entry marker could
  not hold is refused rather than rewritten.
- An append **replaces the file** — read, render, `os.replace` — under an exclusive `flock` on the
  threads **directory**, so two processes never interleave a write. It was an `O_APPEND` write under
  a lock on the file itself until 2026-09-20: a `QFileSystemWatcher` directory watch, which is what
  the Switchboard pane holds, does not fire when an existing file grows, and about two of every
  three writes never reached the pane (#N5JJ). The lock is on the directory because `os.replace`
  gives the path a new inode. Threads are otherwise never rewritten in place except by
  `check --fix` (re-sort).
- **An agent's entry is written by the worker, and card #CTRN did not move a byte of it**
  (2026-09-21). A card's Discuss and Plan became ordinary console turns that day — a queue, thinking
  bubbles, tool rows — and the thread they leave is the same file it was: the owner's entry
  (`author=owner kind=comment mode=discuss|plan`) written before the model sees the words, the
  board's own stage event (`author=owner kind=event`, with `pane=` when the writer named a pane), and the agent's answer
  (`author=agent kind=comment mode= model=<the model that answered> turn=<session>/<turn>`),
  attributes in insertion order, no heading line above them. `relay-board.py check` validates the
  entry id, the `kind` and the ordering but not `author`, `mode`, `model` or `turn`, so drift there
  would be silent: the change was checked instead by diffing a driven thread against one the same
  driver produced on the commit before it
  (`docs/qa_evidence/2026-09-21-card-turns-backend/`).
- The body of the card is the document QA reads (request, decisions, plan, checklist, verdicts);
  the thread is the conversation and the audit trail. Every agent write appends an entry.

## 4. `issues/board.yaml`

```yaml
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: design, folder: design}, {id: marketing, folder: marketing},
  {id: planning, folder: planning},
  {id: deferred, filter: "status:deferred"}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, planning, planned, executing, needs-verification, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
```

A tab names either a `folder` (a category) or a `filter` across categories. Its presence is also
the switch that tells Relay and the agents that this repository has a Switchboard.

**Sections.** `columns:` is the ordered list of sections the one list is divided into, and two
optional keys say the rest:

```yaml
columns: [inbox, discussing, planning, planned, executing, needs-verification, needs-qa, done, research]
column_statuses: {research: []}
column_titles: {needs-qa: Checks, research: Research}
```

- `column_statuses:` is what a section collects, overriding the defaults in `board.COLUMN_STATUSES`
  — this is how two sections **merge** into one, and how a board **invents** a section of its own.
  A column id outside `COLUMN_IDS` is only allowed when this key names it, so a typo is
  still caught. **One status belongs to exactly one section**: two would draw the same card twice.
- An **explicit empty list is a manual section** (2026-09-20, #3XZV): a section that collects
  nothing and is filled by hand — `board_move_card {section: research}` parks a card in it (the
  `section:` front-matter field above), a drop or a quick-add in the pane does the same, and the
  card's status stays what it was. Removing the section from `columns:` leaves the parked cards'
  `section:` dangling, which `check` reports as a warning.
- `column_titles:` is what a section is *called*, over an id that does not change. Renaming is a
  display name and nothing else; `""` puts the Relay wording back. It may name a section that is
  not in `columns:` — a plan's `draft`, `deferred`, `verified`, `done` — because those sections
  exist whenever a card has that status.
- **No card moves and no status changes** for any of these, which is why the whole section list is
  a config write. A status that no section collects is not hidden: it gets a section of its own,
  after the configured ones (`Model::sections()`, `src/BoardModel.cpp`).

Add, remove, merge, rename and move are one write of this file, from any of three places: the gear
at the end of the section checkboxes in the Switchboard pane, the agent's `board_sections` tool
during a cleanup run, or your editor. Moving a section rewrites `columns:` in the new order and
touches no card; the two that are always last (Verified, Done) do not move and nothing moves past
them. The first two go through one validator and land in the write log, so `board_undo` takes a
section change back like any other write.

`issues/BOARD.md` is generated by `relay-board.py index`: one table per tab, ordered by status then
rank, with the id, the title linked to the file, the assignee, task progress and the thread link.
It is never hand-merged — regenerate it.

## 4.1 `issues/POLICY.md` and the instruction-file block

A **guest** agent — Claude Code or Codex started in a Relay pane (`AGENT-SESSIONS-PROTOCOL.md`
§26–29) — never sees the worker's system prompt and has no `board_*` tools, so nothing in
`board_policy.md` reaches it. What it reads is the project's instruction files. So the rules are
also a file in the board, and the instruction files point at it (card #R9G7, owner: "tell claude md
and agents md to read the relay system prompt").

`<board>/POLICY.md` is **generated**, like `BOARD.md`, by `board.policy_text(board)`, and its first
line says so. Three sources, in this order: the body of `backend/relay_core/board_policy.md` (the
same bytes the worker puts in its own system prompt, with the provenance comment stripped, so a
guest and a pane agent are told the same thing), the body of the bundled `deliver` skill
(`relay_core/skills_bundled/deliver/SKILL.md`, its headings demoted a level), and an appendix,
**"Without the board tools"**, that maps each `board_*` call the other two name onto the file edit
that does the same job: reading `BOARD.md` and the card files, a new card file with the front matter
of section 2, a claim as `status: executing` plus `assignee:` — never `session:`, which is Relay's
pane token and immutable (section 2.2) — a thread entry in the format of section 3, a status change
with the file move of section 1, and `relay-board.py check` as the validator. The board's own folder
name is substituted throughout, and a hidden `.switchboard/` carries the `rg --hidden` note from the
top of this document.

The pointer is a marked block, `<!-- relay:switchboard-policy start -->` … `<!-- … end -->`, naming
the board folder and telling the reader to read `POLICY.md` before doing work. `board.scaffold_files`
appends it to `CLAUDE.md` and to `AGENTS.md` when they exist, and **creates `AGENTS.md`** when the
project has none — including a project that has only a `CLAUDE.md`, because both files are meant to
point at the policy. A created `AGENTS.md` starts with an `@CLAUDE.md` import (and `@CLAUDE.local.md`
when there is one): `relay_core.instructions` loads the *first* hit per directory in `PROJECT_ORDER`
(`WARP.md`, `AGENTS.override.md`, `AGENTS.md`, `CLAUDE.md`, …), so a bare new `AGENTS.md` would
shadow the project's own `CLAUDE.md`, and the import is what keeps that text in every prompt.
`WARP.md` is never written: it is first in that order, so it is what Relay's own agent reads — and
that agent has the policy in its system prompt already.

Nothing outside the markers is read, moved or rewritten, a block that is already current is not
rewritten at all, and a file holding one marker without the other is left for a person. Both files
are generated, so a **stale** copy is a missing one: `scaffold_files` rewrites `POLICY.md` and
replaces the block whenever the rendering has changed, `Board.write_index` refreshes an existing
`POLICY.md` (it never creates one), and `relay-board.py policy` does both on a board that predates
them. `POLICY.md` at the board root is not a card, so `check` ignores it as it does `BOARD.md`.

## 5. Privacy

`issues/.private/` is the single private root: private cards, their threads, their plans and
private memory (`.private/<category>/`, `.private/threads/`, `.private/memory/`). It is gitignored
through `issues/.gitignore`, and a private card also carries `private: true`; `check` reports a
card whose flag and location disagree, and any private file that git is tracking. Caveats: a git
worktree does not see it and `git clean -fdx` deletes it.

Two per-machine files live there beside the cards, and neither is a card: `forge-sync.json` (the
GitHub sync's state, `docs/GITHUB-SYNC.md` §3) and `forge-logins.yaml` — or `.json` — the person →
GitHub login map, which is per user rather than committed. `Board.card_paths()` only walks `*.md`
two levels down, so `check` ignores both.

### 5.1 The machine's two stores: run history and signals

`.private/` also holds what the *machine* records about this checkout, in two append-only JSONL
files. Both are per machine on purpose — they describe runs of *this* working tree — and neither is
ever committed; the thing another clone should see is the card a signal was promoted into.

| Path | One line is | Written by | Read by |
|---|---|---|---|
| `.private/tests/history.jsonl` | one **test execution**: `{ts, run_id, id, runner, result, duration, commit, host, message?, excerpt?, source_hash?, tree_digest?}` | `test_history.append`, from `ingest_junit` | `test_history.records()` (the Test suites pane, Check), `signals.fold()` |
| `.private/tests/incoming/<host>-<run>/` | a run fetched from another machine: `meta.json`, `ctest.xml`, `unittest.xml`, logs, and an `ingested` marker once folded in | `scripts/relay-remote-tests` | `tests_protocol.ingest_incoming`, exactly once per folder |
| `.private/signals/events.jsonl` | one **action** on a signal: `{ts, v, action, key?, session?, reason?, comment?, until?, card?, by?, run_id?}` | `signals.append_event` | `signals.fold()` |

`tree_digest` on an execution is empty when the working tree was `commit`'s, and otherwise twelve
hex of sha256 over `git diff HEAD`: several sessions edit this one checkout, so a commit does not
identify the code that ran, and the flake rule that Datadog states over commits is read over
`(commit, tree_digest)` instead (card `#AQ6X`, research R2).

`action` is one of `claim`, `release`, `dismiss`, `promote`, `note` and `run`. A **signal** is not
stored: it is the fold of the executions and these actions, so the log holds only what a person or
an agent *did*, and an occurrence is never written twice (`docs/SIGNALS-RESEARCH.md` R1, R13). The
`run` lines are the one thing the executions cannot say — which pane's run a failure came from —
and they are what the verification gate reads. `AGENT-SESSIONS-PROTOCOL.md` §32 is the contract;
`backend/relay_core/signals.py` is the fold and every threshold.

A signal's only committed trace is its promoted card: an ordinary `work` card in the bugs tab with
labels `bug` and `signal`, `links.signal` naming the key, and a machine-owned `## Signal` section
that Relay **rewrites in place** on every state change. Treat that section like `implemented_by`:
generated, never hand-edited.

## 6. `scripts/relay-board.py`

| Command | Does |
|---|---|
| `check [--fix] [--json] [--strict]` | the rules below; exit 1 on any error (`--strict`: on warnings too) |
| `index [--stdout] [--private]` | regenerate `issues/BOARD.md` |
| `policy [--stdout]` | regenerate `issues/POLICY.md` and the pointer block in `CLAUDE.md` / `AGENTS.md` (section 4.1) |
| `migrate [--apply]` | convert a pre-board tracker (dry run by default) |

`check` rules — errors: folder and status disagree; duplicate id; missing, malformed or lowercase
id; unknown status or card type; unknown front matter field for the type; no front matter; no `# `
title; unresolved merge markers; bad rank; `private:` flag not matching the location; a private
file tracked by git; duplicate item id; a malformed task marker; `blocked_by` naming an item that
is not there; a `blocked_by` cycle; duplicate thread entry ids; a thread entry id that is not a
sortable timestamp; an unknown entry kind; a thread file not named `<ID>.md`.
Warnings: an item with no marker, a checkbox its marker disagrees with, entries out of id order
(all three fixable with `--fix`), an item nested deeper than one level, a missing rank, a thread
with no card, a work card's `## ` heading outside the section schema (`unknown_section`,
section 2.7).

`migrate` is deterministic and reversible by review: it parses the `- **Field**: value` header
block into front matter (wrapped values joined with a space), keeps the H1 and every section
byte-for-byte, derives each id from the file path, assigns evenly spaced ranks per category in path
order, maps `open` → `ready`, keeps the `needs_qa_llm/` paths (`status: needs-qa-llm`; where a
header and its folder disagree the folder wins and the difference is reported), and creates
`board.yaml`, `threads/`, `issues/.gitignore`, the `.gitattributes` line and `BOARD.md`. Files it
cannot parse are listed and left untouched; running it twice changes nothing.

## 7. Paragraph for the global `issue-tracking` skill

*(For the owner to paste into `~/.warp/skills/issue-tracking/SKILL.md` — no file outside this
repository was edited. Suggested position: after "Header fields".)*

> **Board format.** If the repository has an `issues/board.yaml`, its issues are *cards*: YAML front
> matter (`id`, `type`, `status`, `rank`, `labels`, `links`, and the header fields above as keys)
> followed by the Markdown body, whose first `# ` heading is the title. Keep using the folders —
> folder and status must agree, and `open` is written `ready` — and keep one file per issue. Put
> discussion, decisions and every write you make in the card's append-only thread
> `issues/threads/<ID>.md` (one self-contained entry per append, never rewriting earlier ones),
> not in the body; the body stays the document a QA session reads. Checklist items in `## Tasks`
> carry `<!-- t:xx -->` markers, and the checkbox wins over the marker. Cards are referenced as
> `#K7Q2` and items as `#K7Q2.a3`. Run `scripts/relay-board.py check` before committing, and
> `index` to regenerate `issues/BOARD.md`. `type: memory` cards (in `memory/`) and `type: alias`
> cards (in `aliases/`) use the same format. Repositories without `issues/board.yaml` are unaffected.
