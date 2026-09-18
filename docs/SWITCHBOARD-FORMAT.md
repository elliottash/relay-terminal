# Switchboard file format (reference, phase 0)

The Switchboard **is** `issues/`: plain Markdown in git that works without Relay, on GitHub and in
any editor. This is the normative reference for the bytes; the product design is in
[`SWITCHBOARD-DESIGN.md`](SWITCHBOARD-DESIGN.md) and [`TASKS-AND-MEMORY-DESIGN.md`](TASKS-AND-MEMORY-DESIGN.md).

Implementation: `backend/relay_core/board.py` (parsing, ids, ranks, task markers, thread appends,
atomic hash-checked writes, the check rules) and `scripts/relay-board.py` (`check`, `index`,
`migrate`). Neither calls a model or the network. Phase 0 ships the format and the tooling only:
no agent tools, no UI, no note scanning, no GitHub sync.

## 1. Tree

```text
issues/board.yaml              tabs, columns, autonomy (committed config)
issues/BOARD.md                generated index; never hand-edited
issues/<category>/             features/ changes/ design/ marketing/ … = tabs
issues/<category>/<state>/     needs_qa_llm/ needs_qa_human/ needs_review/ needs_labels/
                               needs_ab/ deferred/ done/
issues/planning/               plan cards (type: plan), done ones in planning/done/
issues/memory/                 memory cards (type: memory), retired ones in memory/archive/
issues/aliases/                alias cards (type: alias), retired ones in aliases/archive/
issues/threads/<ID>.md         one append-only thread per card
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
reviewed, author, supersedes, approved_by, goal, label_count, label_output, codebook, rank,
created, acceptance, source, links`, then any other key, sorted).

### 2.2 Fields

Every type: `id`, `type`, `status`, `rank`, `created`, `labels`, `assignee`, `private`, `links`,
`aliases`, `source`, `blocked_by`, `parent`, `waiting_on`.

| Type | Extra fields | `status` | Folder |
|---|---|---|---|
| `work` (default) | `component`, `milestone`, `workstream`, `acceptance`, `implemented_by`, `label_count`, `label_output`, `codebook` | `inbox`, `discussing`, `ready`, `in-progress`, `needs-review`, `needs-labels`, `needs-ab`, `needs-qa-llm`, `needs-qa-human`, `deferred`, `done`, `dropped` | `<category>/` plus the state subfolder |
| `plan` | `approved_by`, `goal` | `draft`, `approved`, `executing`, `done`, `dropped` | `planning/`, `planning/done/` |
| `memory` | `name`, `description`, `kind`, `topic`, `scope`, `paths`, `pinned`, `supersedes`, `reviewed`, `author` | `active`, `retired` | `memory/`, `memory/archive/` |
| `alias` | `name`, `kind`, `shell` | `active`, `retired` | `aliases/`, `aliases/archive/` |

- `ready` is what the old tracker called `open`; `dropped` cards live in `done/` beside `done` ones.
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

`issues/BOARD.md` is generated by `relay-board.py index`: one table per tab, ordered by status then
rank, with the id, the title linked to the file, the assignee, task progress and the thread link.
It is never hand-merged — regenerate it.

## 5. Privacy

`issues/.private/` is the single private root: private cards, their threads, their plans and
private memory (`.private/<category>/`, `.private/threads/`, `.private/memory/`). It is gitignored
through `issues/.gitignore`, and a private card also carries `private: true`; `check` reports a
card whose flag and location disagree, and any private file that git is tracking. Caveats: a git
worktree does not see it and `git clean -fdx` deletes it.

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
