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
`migrate`). Neither calls a model or the network. This document stays the format alone; what reads
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
issues/<category>/             features/ changes/ design/ marketing/ … = tabs
issues/<category>/<state>/     needs_qa_llm/ needs_qa_human/ needs_review/ needs_labels/
                               needs_ab/ deferred/ done/
issues/planning/               plan cards (type: plan), done ones in planning/done/
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

A card file keeps the name it was created with (`YYYY-MM-DD-slug.md` for work and plan cards,
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
(`id, type, status, name, description, kind, topic, scope, private, labels, component, milestone,
workstream, assignee, implemented_by, waiting_on, parent, blocked_by, aliases, paths, pinned,
reviewed, author, supersedes, approved_by, goal, label_count, label_output, codebook, priority,
rank, created, acceptance, source, links`, then any other key, sorted).

### 2.2 Fields

Every type: `id`, `type`, `status`, `priority`, `rank`, `created`, `labels`, `assignee`,
`private`, `links`, `aliases`, `source`, `blocked_by`, `parent`, `waiting_on`.

`priority` (2026-09-20, #VKFV) is the row's flag in the Switchboard: an integer from −1 to +3.
It is written only when nonzero — a card with no flag carries no `priority` key — and a value
outside the range is clamped on every write.

| Type | Extra fields | `status` | Folder |
|---|---|---|---|
| `work` (default) | `component`, `milestone`, `workstream`, `acceptance`, `implemented_by`, `verified_by`, `label_count`, `label_output`, `codebook` | `inbox`, `discussing`, `ready`, `in-progress`, `needs-review`, `needs-labels`, `needs-ab`, `needs-qa-llm`, `needs-qa-human`, `deferred`, `done`, `dropped` | `<category>/` plus the state subfolder |
| `plan` | `approved_by`, `goal` | `draft`, `approved`, `executing`, `done`, `dropped` | `planning/`, `planning/done/` |
| `memory` | `name`, `description`, `kind`, `topic`, `scope`, `paths`, `pinned`, `supersedes`, `reviewed`, `author` | `active`, `retired` | `memory/`, `memory/archive/` |
| `alias` | `name`, `kind`, `shell` | `active`, `retired` | `aliases/`, `aliases/archive/` |

- `ready` is what the old tracker called `open`: **agreed and not started**. The pane labels the
  section **Ready to start**, because "Ready" on its own was read as "ready to ship" (owner,
  2026-09-19); the status id is unchanged, so no card file or folder moved. Every section's
  one-clause meaning lives in `board::sectionMeaning()` (`src/BoardModel.cpp`) and is what the
  header tooltips and the hide checkboxes show.
- `dropped` cards live in `done/` beside `done` ones.
- `implemented_by` and `verified_by` are **signatures, not free text**, and Relay writes them: the
  worker stamps `provider/model` from its own preset and model when a card enters `in-progress` or a
  QA lane, and again when it leaves a QA lane to `done` (card `#T71W`, protocol section 19.15). The
  provider segment is the *model's* vendor, never the aggregator that routed to it, so a card served
  `deepseek/deepseek-v4.1-flash` through OpenRouter reads `deepseek/deepseek-v4.1-flash` and a local
  endpoint reads `local/<model>`. A guest CLI names the model it ran and the harness that ran it,
  `anthropic/claude-opus-5-20260514 via claude-code`, falling back to `anthropic/claude-code` or
  `openai/codex` when the model cannot be seen. Free text in parentheses is allowed and ignored
  (`anthropic/claude-opus-5 (pane 2)`), which is how the hand-typed values written before 2026-09-19
  keep working. The pair is what `relay-board.py verifier <ID>` answers from. `verified_by` is never `relay-free/…`: verifying is
  not available on the free plan (owner, 2026-09-19), and a close signed by it is refused.
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
- Appends use `O_APPEND` under `flock`, so two processes never interleave a write. Threads are
  never rewritten in place except by `check --fix` (re-sort).
- The body of the card is the document QA reads (request, decisions, plan, checklist, verdicts);
  the thread is the conversation and the audit trail. Every agent write appends an entry.

## 4. `issues/board.yaml`

```yaml
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: design, folder: design}, {id: marketing, folder: marketing},
  {id: planning, folder: planning},
  {id: deferred, filter: "status:deferred"}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
```

A tab names either a `folder` (a category) or a `filter` across categories. Its presence is also
the switch that tells Relay and the agents that this repository has a Switchboard.

**Sections.** `columns:` is the ordered list of sections the one list is divided into, and two
optional keys say the rest:

```yaml
columns: [inbox, discussing, ready, in-progress, needs-qa, done]
column_statuses: {needs-qa: [needs-qa-llm, needs-qa-human, needs-review, needs-labels, needs-ab]}
column_titles: {needs-qa: Checks, ready: Up next}
```

- `column_statuses:` is what a section collects, overriding the defaults in `board.COLUMN_STATUSES`
  — this is how two sections **merge** into one, and how a board **invents** a section of its own.
  A column id outside `COLUMN_IDS` is only allowed when this key gives it statuses, so a typo is
  still caught. **One status belongs to exactly one section**: two would draw the same card twice.
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

## 6. `scripts/relay-board.py`

| Command | Does |
|---|---|
| `check [--fix] [--json] [--strict]` | the rules below; exit 1 on any error (`--strict`: on warnings too) |
| `index [--stdout] [--private]` | regenerate `issues/BOARD.md` |
| `migrate [--apply]` | convert a pre-board tracker (dry run by default) |

`check` rules — errors: folder and status disagree; duplicate id; missing, malformed or lowercase
id; unknown status or card type; unknown front matter field for the type; no front matter; no `# `
title; unresolved merge markers; bad rank; `private:` flag not matching the location; a private
file tracked by git; duplicate item id; a malformed task marker; `blocked_by` naming an item that
is not there; a `blocked_by` cycle; duplicate thread entry ids; a thread entry id that is not a
sortable timestamp; an unknown entry kind; a thread file not named `<ID>.md`.
Warnings: an item with no marker, a checkbox its marker disagrees with, entries out of id order
(all three fixable with `--fix`), an item nested deeper than one level, a missing rank, a thread
with no card.

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
> `index` to regenerate `issues/BOARD.md`. `type: plan` and `type: memory` cards (in `planning/`
> and `memory/`) use the same format. Repositories without `issues/board.yaml` are unaffected.
