# Initializing a project, and importing what is already there

A project gets a Board only when the user answers one question, once:

> **Initialize a project and create a Board here?**

Owner's rule, 2026-09-18. Nothing creates `<project>/board/` before that yes — not an
agent, not opening the Board pane, not a `board_*` tool
(`board_tools.BoardTools.create_board` is the only path, and every route into it goes through
the `board_init_request` round trip).

This document covers the other half of that question: **what it shows**. Most projects that
are worth initializing already keep their work somewhere — a `TODO.md`, a `backlog/`, a
`tasks.json`, a `specs/` tree. The question shows what Relay found, offline, and offers to
bring it across. The offer is a checkbox per finding and **every checkbox starts unchecked**:
saying yes to a Board is not saying yes to an import.

Implementation: `backend/relay_core/project_probe.py` (finding) and
`backend/relay_core/board_import.py` (proposing, writing). The board format itself is
`docs/SWITCHBOARD-FORMAT.md`; the product design, including §7 "Detecting and converting
non-compliant notes", is `docs/SWITCHBOARD-DESIGN.md`.

---

## 1. What the question shows

```text
┌─ Initialize a project and create a Board here? ───────────────┐
│  /home/elliott/src/widgetworks                                      │
│                                                                     │
│  This project already has                                           │
│   ☐  23 tasks in backlog/  (3 in completed/ and archive/ are left   │
│      alone)                                                         │
│   ☐  TODO.md: 6 unchecked items, 1 headed section                   │
│   ☐  4 feature specs in specs/ with 61 checklist items, 44 not done │
│                                                                     │
│  Also here, not imported                                            │
│   ·  github.com/widgetworks/widgetworks — issues would be filed on  │
│      upstream (origin looks like a fork)                            │
│   ·  3 GitHub issue templates in .github/ISSUE_TEMPLATE             │
│   ·  the branch name carries ENG-1204, which looks like a Jira or   │
│      Linear key; Relay does not contact either                      │
│                                                                     │
│                                   [ Not now ]  [ Create Board ]│
└─────────────────────────────────────────────────────────────────────┘
```

Since 2026-09-19 this is **inline in the pane**, not a dialog: a block in the pane's own column
directly under the terminal, beside the thinking panel and the queue strip, so the terminal
reflows into what is left and nothing floats over it (the owner's rule — new surfaces are panes or
inline in the pane). It carries three buttons, `[ Not now ] [ No ] [ Yes ]`, and answers to `y`,
`n` and `Esc` with Tab walking the checkboxes. `src/ProjectInitBlock.h` draws it;
`src/ProjectInit.h` decides when it appears and what every line of it says.

**Not now is not a no.** A no is remembered on disk (`projects::Registry::decline()`) and nothing
asks again, in any tab, after any restart, until `/init`. "Not now" only stops the quietest
trigger — the first prompt sent to the agent — for the rest of that Relay session, and every
explicit act asks again. The five acts that may ask at all, and nothing else, are listed in
`AGENT-SESSIONS-PROTOCOL.md` §19.12.

**What creating a board writes.** `board.scaffold` writes `board.yaml`, `threads/`, the board's
`.gitignore` and the `.gitattributes` union-merge line — and, since card #R9G7, the two files that
carry the Board's rules to an agent that has no `board_*` tools. `<board>/POLICY.md` is those
rules, generated from `board_policy.md` and the bundled `deliver` skill with an appendix that says
how to file, claim, comment on and move a card by editing files; and a marked block is appended to
the project's `CLAUDE.md` and `AGENTS.md` pointing at it, with `AGENTS.md` created when the project
has none (it starts with an `@CLAUDE.md` import so it cannot shadow the project's own instructions,
and `WARP.md` is never touched). Both are generated, so a stale copy is rewritten and nothing
outside the markers is changed; the files appear in the `board_created` event's `files` list like
the rest. The format doc has the detail (`SWITCHBOARD-FORMAT.md` §4.1), and
`scripts/relay-board.py policy` regenerates them for a board that predates them. This is the one
part of initialization that writes outside the board folder, and it writes instruction files only:
no card, no code, nothing else in the project.

Three rules the GUI must keep:

1. **The checkboxes are unchecked.** Importing is a separate decision from initializing.
2. **Nothing has happened yet.** The probe read files. No card exists, no directory was made,
   nothing was written, and — see §2 — nothing left the machine.
3. **A project that already has a board is never asked.** `board.present` is true and the
   dialog does not open. A project with a *pre-board* `issues/` tree is asked, and the offer
   is `migrate`, which is a different operation from an import (it converts the files in
   place; see `docs/SWITCHBOARD-FORMAT.md` §6).

### The one path that does not ask: "Initialize new project here"

Since 2026-09-19 (card #916B) there is a sixth way a board comes to exist, and it is the one that
asks nothing. In a pane standing in no project at all — `~/Downloads`, an admin folder — reaching
for the Board opens the **project picker** (`src/ProjectPicker.h`): the projects Relay knows,
and, always first, **"Initialize new project here"**. Choosing that row *is* the explicit user
action the consent rule exists for, so the question above is not shown a second time and no probe
runs: `Pane::initProjectHere()` attaches the tab to the pane's own directory and sends
`board_init {project, git_init: true}` once the attach has landed, exactly as a yes would. The
worker creates the board and, because a directory nobody has made a project of is usually not a
repository either, runs `git init` there — **only** when the directory is not inside a repository
already: an existing checkout is never re-initialised and a parent repository's config is never
touched. What it did comes back on the answering `board_state` as one line (`git: "git repository
initialized"` / `"already a git repository"` / `"inside the git repository at …, which was left
alone"`), and the pane repeats it on its created line. A `/card` typed in that pane is held through
the picker and filed as the new board's first card. A directory the user once said no to may be
chosen here all the same — choosing it is changing one's mind, and `Registry::remember()` clears
the no. The import offer (§1) is not made on this path; `/init` in the new project still asks, and
still offers it.

---

## 2. The probe is offline, read-only and bounded

`project_probe` never opens a socket, never runs a subprocess and never writes a file.
`tests/test_project_probe.py::OfflineTest` replaces `socket.socket`,
`socket.create_connection`, `socket.getaddrinfo`, `subprocess.Popen`, `subprocess.run`,
`subprocess.check_output`, `os.popen` and `os.system` with functions that raise, and probes
every fixture tree.

Two reasons, and the second is the less obvious one:

* the project has a **no-telemetry rule**, and this code runs before the user has agreed to
  anything at all;
* **running `git` would be as bad as opening a socket.** The probe runs in a directory nobody
  has vetted, and `git` there reads that repository's config: a hook, a credential helper, a
  `core.fsmonitor` program. So `.git/config` is parsed as a *file*, `.git/HEAD` is read as a
  *file*, and git is never executed.

Bounds, all in `project_probe`'s constants:

| Constant | Value | What it stops |
|---|---|---|
| `MAX_FILE_BYTES` | 512 KiB | a generated `TODO.md` |
| `MAX_JSON_BYTES` | 4 MiB | a large but legitimate `tasks.json` or JSONL export |
| `MAX_ITEMS_PER_SOURCE` | 500 | a file with 40 000 checkboxes (the finding says `truncated`) |
| `MAX_SOURCES_PER_KIND` | 40 | 900 directories under `specs/` |
| `MAX_DIR_ENTRIES` | 2000 | one enormous directory |
| `MAX_DEPTH` | 3 | a deep tree walked to the bottom |

A file over its ceiling is **skipped, not truncated**: half a `tasks.json` is not a tracker.

**Symlinks never leave the project.** Every read and every directory listing goes through
`_inside()`, which resolves the path and checks it is still under the project root, so a
`docs -> /etc` symlink is a finding of nothing. The single deliberate exception is a `.git`
*file*: a submodule's `gitdir:` legitimately points into the superproject's
`.git/modules/<name>`, so that one pointer is followed, once, with no further indirection,
and only a `config`, a `commondir` and a `HEAD` are read from what it names.

---

## 3. `probe()` and its JSON

```python
project_probe.probe(project, *, gh_hosts_path=None, env=None, kinds=None) -> dict
```

Sorted, JSON-serialisable, and small — an empty project is under 2 KB. It never raises for a
project it cannot read: an unreadable corner is an absent finding, because a traceback in that
dialog helps nobody.

```jsonc
{
  "version": 1,
  "project": "/home/elliott/src/widgetworks",     // always absolute

  "git": {
    "is_repo": true,
    "remotes": [                                   // sorted by name
      {"name": "origin",   "url": "git@github.com:me/widgetworks.git",
       "host": "github.com", "owner": "me", "repo": "widgetworks", "forge": "github"},
      {"name": "upstream", "url": "https://github.com/widgetworks/widgetworks.git",
       "host": "github.com", "owner": "widgetworks", "repo": "widgetworks", "forge": "github"}
    ],
    "primary": "upstream",
    "primary_reason": "upstream over origin: origin looks like a fork, and issues are filed on the repository it was forked from",
    "branch": "feature/ENG-1204-dark-mode",
    "ticket_keys": [{"key": "ENG-1204",
                     "pattern": "[A-Z][A-Z0-9]{1,9}-[0-9]{1,6}",
                     "looks_like": "jira-or-linear"}]
  },

  "board": {"present": false, "kind": "none",      // "board" | "pre-board" | "none"
            "folder": null, "path": null, "cards": 0, "convertible": 0},

  "trackers": [                                    // sorted by (kind, path)
    {"kind": "backlog-md", "path": "backlog", "count": 23,
     "summary": "Backlog.md in backlog/: 23 task(s), 20 not done; 3 in completed/ and archive/ are left alone"},
    {"kind": "checklist", "path": "TODO.md", "count": 7,
     "summary": "TODO.md: 6 unchecked item(s), 1 headed section(s)"}
  ],

  "hints": [                                       // things Relay will NOT import
    {"kind": "github-issue-templates", "path": ".github/ISSUE_TEMPLATE", "count": 3,
     "detail": "this project files issues on GitHub with 3 issue template(s)"},
    {"kind": "ticket-key-in-branch", "path": ".git/HEAD", "count": 1, "key": "ENG-1204",
     "pattern": "[A-Z][A-Z0-9]{1,9}-[0-9]{1,6}",
     "detail": "the branch name carries ENG-1204, which looks like a Jira or Linear key; Relay does not contact either"}
  ],

  "counts": {"trackers": 2, "items": 30},
  "gh_hosts": ["code.acme.example", "github.com"]
}
```

A tracker finding is exactly `{kind, path, count, summary}`, plus `"truncated": true` when the
item ceiling bit. **The items themselves are not in the probe result** — that is what keeps it
small. `project_probe.items_for(project, kinds)` re-reads them when the user asks to import,
using the same parsers, so there is one reader per format and not two.

### `forge`, and how GitHub Enterprise is recognised

`forge` is one of `github`, `github-enterprise`, `gitlab`, `gitea-like`, `bitbucket`,
`azure-devops`, `unknown`. `github.com`, `gitlab.com`, `bitbucket.org`, `dev.azure.com`,
`codeberg.org` and friends are known by name; `gitlab.*`, `gitea.*`, `forgejo.*` and
`*.visualstudio.com` are recognised by their first label.

A GitHub Enterprise host has **no giveaway in its name** — it is whatever the company called
it. The only offline evidence is that the user's own `gh` is logged in to it, so the probe
reads `hosts.yml` and treats any host listed there (other than `github.com`) as
`github-enterprise`. It reads the **top-level keys only** — the hostnames — and never
descends into a host's settings, so no `oauth_token` is ever loaded. The path is
`$GH_CONFIG_DIR`, else `$XDG_CONFIG_HOME/gh`, else `~/.config/gh`, and it is injectable
(`gh_hosts_path=`) so tests never read the developer's own.

### `primary`, and why `upstream` wins

`upstream` beats `origin` beats everything else (then alphabetical). On a fork, `origin` is
the user's own copy and the issues live on the repository it was forked from. `primary_reason`
is a sentence written for the dialog, not a code — show it.

### `insteadOf`

`url.<base>.insteadOf` rewrites are applied from **the repository's own config only**. The
longest matching prefix wins, as git does. `~/.gitconfig` is deliberately not read: it would
report a URL the user never wrote into this project, and the probe stays inside the project.

---

## 4. What is found, and how it maps onto a card

Seven kinds, `project_probe.TRACKER_KINDS`. Every parser is written against the format's own
documentation, and the URL is in the parser's docstring — §7 lists what could not be verified
that way.

Common to all of them:

| Card field | Comes from |
|---|---|
| title (`# ` heading) | the item's own title, cut to 200 characters |
| `## Issue` | the item's description, with any extra sections demoted to `###` |
| `## Tasks` | the item's own checklist, each item keeping its state |
| `labels` | `imported`, the tracker's name, then the source's labels (12 max) |
| `status` | §4.1 |
| `rank` | creation order, which is priority order — §4.2 |
| `source` | `relay-import: "<source_key>" line N` — §5 |
| `blocked_by` | dependencies, resolved to cards created in the same run — §4.3 |
| thread | one `note` entry naming the file it came from, plus any comments the source carried |

### 4.1 Statuses

`board_import.STATUS_MAP`. The rule: **anything a tracker calls "not started" becomes
`inbox`**, because an untriaged item is exactly what Inbox is for, and a tracker that says
more than that is taken at its word.

| Source says | Card status |
|---|---|
| `open`, `pending`, `todo`, `To Do`, `backlog`, `draft`, `proposed`, an unchecked box | `inbox` |
| `in_progress`, `In Progress`, `doing`, `active`, Kiro's `- [-]` | `in-progress` |
| `blocked` | `inbox` — there is no blocked column; `blocked_by` carries the fact |
| `review` | `needs-review` |
| `deferred`, `on hold`, `paused` | `deferred` |
| `done`, `closed`, `completed`, `resolved` | `done` |
| `cancelled`, `wontfix`, `duplicate`, `archived` | `dropped` |

Items the tracker has finished **are** proposed (except from checklists, below): they are real
records, dependencies point at them, and the user unticks what they do not want. A checklist's
checked items are ignored outright — §4.4.

### 4.2 Priorities become rank order

The board has no priority field; it has `rank`, a fractional index that orders a column
(`docs/SWITCHBOARD-FORMAT.md` §2.4). So priority is expressed as order: proposals are sorted
`critical < high < medium < low < lowest`, then by the tracker's own order inside one tracker,
and cards are created in that order. `BoardTools` appends each new card to the end of its
column, so each column comes out in the source's priority order. Nothing is renumbered.

Beads' integer priorities are read as `0 = critical, 1 = high, 2 = medium, 3 = low,
4 = lowest`; `0` is a real priority, not a missing one.

### 4.3 Dependencies

`blocked_by` **is** a legal front matter field for a work card
(`board.COMMON_FIELDS`), so a dependency becomes `blocked_by: [K7Q2]`. Two rules:

* ids are resolved **inside one tracker's own numbering** (its `group`: a `backlog/` directory,
  a `tasks.json`, a Task Master *tag*), so Task Master's `master/1` and `feature-auth/1` never
  link to each other;
* a dependency on something **not in this run** is dropped, not invented. A `blocked_by`
  pointing at a card that does not exist would read as a lost card.

A Beads `parent-child` edge becomes the `parent` field, which is also a legal card field.
Beads' non-blocking edge types (`related`, `discovered-from`, `tracks`, …) are not imported.

Dependencies **between the items of one card** are written too, as the `blocked_by=` marker of
`SWITCHBOARD-FORMAT.md` §2.5 (Task Master's subtasks are the only source that has them). The same
two rules hold: a sibling becomes that item's marker, a dependency on another *task* becomes
`blocked_by=#CARD` for the card that task imported as, and anything outside the run is dropped.

### 4.4 The seven, one by one

#### `checklist` — `TODO.md`, `NOTES.md`, `IDEAS.md`, `ROADMAP.md`, `PLAN.md`

At the project root and in `docs/`, matched case-insensitively. Nowhere else: a
`src/vendor/TODO.md` is somebody else's file.

| In the file | Becomes |
|---|---|
| a top-level unchecked box | one card |
| boxes nested under it | that card's `## Tasks`, each keeping its own mark |
| a **checked** top-level box | nothing — ignored (`SWITCHBOARD-DESIGN.md` §7) |
| a `##` section with no boxes in it at all | one card carrying that prose |
| the `# ` title and the paragraph under it | nothing — that is the document, not a card |
| `- [-]`, `- [/]`, `- [~]` | an in-progress item |

`TODO:` comments in source code are out of scope, as the design says: noise.

#### `backlog-md` — MrLesk/Backlog.md

Found at `backlog/`, `.backlog/`, or wherever a root `backlog.config.yml` says with
`backlog_directory:`. Reads `tasks/` and `drafts/`; `completed/` and `archive/` are counted in
the summary and left alone, because a new board filling with finished work helps nobody.

| Backlog.md | Card |
|---|---|
| front matter `id` (`TASK-1`, `BACK-535.2`) | the `source_key`'s id part |
| `title` | the title |
| `status` (free-form, `config.yml` `statuses`) | via §4.1; `To Do` / `In Progress` / `Done` |
| `priority` | rank order |
| `labels`, `type`, and `draft` for a draft | labels |
| `assignee` (a **list**) — first entry | `assignee` |
| `milestone` | `milestone` |
| `dependencies` | `blocked_by` |
| `parent_task_id` | `parent` |
| `## Description` | `## Issue` |
| `## Acceptance Criteria` + `## Implementation Plan` boxes | `## Tasks`, `#1 ` numbering stripped |
| `## Implementation Notes`, `## Final Summary` | kept in the body under `###` headings |
| `ordinal`, else the numeric part of the id | order within the tracker |

Both acceptance-criteria spellings parse: the current `- [ ] #1 text` and the legacy writer's
unnumbered `- [ ] text`.

#### `beads` — steveyegge/beads (the repository now answers as gastownhall/beads)

**Relay reads `.beads/issues.jsonl`, which is an export, not the source of truth.** Beads keeps
its issues in a Dolt database under `.beads/embeddeddolt/` (or `.beads/dolt/`), and the JSONL
is written only when `export.auto` is on. Reading Dolt would mean a dependency and a
subprocess, and this module has neither — so the finding's summary says so in as many words,
and a stale export imports as a stale export.

| Beads | Card |
|---|---|
| `id` | the `source_key`'s id part |
| `title` | the title |
| `description`, `design`, `acceptance_criteria`, `notes` | `## Issue`, the last three under `###` headings |
| `status` (`open`, `in_progress`, `blocked`, `deferred`, `closed`, `pinned`, `hooked`) | via §4.1 |
| `priority` (int 0–4) | rank order |
| `labels` + `issue_type` | labels |
| `assignee`, else `owner` | `assignee` |
| `dependencies[]` with type `blocks` / `conditional-blocks` / `waits-for` | `blocked_by` |
| `dependencies[]` with type `parent-child` | `parent` |
| `comments[]` | one thread entry each, under the original author |

Skipped: a `{"_schema": …}` header line, `{"_type": "memory"}` rows, anything whose `_type` is
not `issue`, and `"status": "tombstone"` (a legacy deletion). Some of beads' own docs write
`type` where the Go struct says `issue_type`; both are read.

#### `taskmaster` — eyaltoledano/claude-task-master

`.taskmaster/tasks/tasks.json`, else the legacy `tasks/tasks.json`, else a root `tasks.json`.
Both top-level shapes are read, discriminated the way Task Master's own `hasTaggedStructure`
does it — *any top-level value that is an object with an array `tasks`* — rather than by
looking for a `master` key:

* tagged (v0.17+): `{"<tag>": {"tasks": [...], "metadata": {...}}, …}`
* legacy: `{"tasks": [...], "metadata": {...}}`, read as the tag `master`.

| Task Master | Card |
|---|---|
| `<tag>/<id>` | the `source_key`'s id part; the tag is also a label unless it is `master` |
| `title` | the title |
| `description`, `details`, `testStrategy`, `acceptanceCriteria` | `## Issue` + `###` sections |
| `status` (`pending`, `in-progress`, `review`, `done`, `deferred`, `cancelled`, `blocked`) | via §4.1 |
| `priority` (`high`/`medium`/`low`) | rank order |
| `dependencies` | `blocked_by`, scoped to the tag |
| `subtasks[]` | `## Tasks`, one item each, keeping its own status |
| `subtasks[].dependencies` | `blocked_by=` on that item: a sibling by its marker, another task by its card |

One card per **task**, not per subtask: a subtask is a checklist item, which is what the board
calls the same thing. Ids are read as `int | string` and dependencies likewise, because real
files hold both although the docs say number. Inside a subtask's `dependencies`, a bare number is
a **sibling** subtask (Task Master's own rule); `"4.2"` is task 4's second subtask, and when task
4 is not this one it is read as a dependency on task 4's card, because the board has no way to
name an item of a card it is not on.

#### `spec-kit` — github/spec-kit

`specs/<NNN-slug>/tasks.md`, where the directory is three-or-more digits and a slug, or
`YYYYMMDD-HHMMSS-<slug>` in the timestamp mode. Anything else under `specs/` is left alone, so
a project that merely keeps written specs there is not mistaken for a spec-kit project.

**One card per feature directory**, with that feature's whole `tasks.md` as its `## Tasks`. A
card per `T001` line would put fifty cards on the board for one feature and lose the thing
that made them a set.

| spec-kit | Card |
|---|---|
| `spec.md`'s `# ` title, else `plan.md`'s, else the directory name humanised | the title |
| `spec.md`'s intro, else its `## Overview` | `## Issue` |
| `- [ ] T001 [P] [US1] Description` | a `## Tasks` item, `T001 Description` (the `[P]` and `[US<n>]` tags are dropped) |
| how many boxes are ticked | the card's status (none → `inbox`, some → `in-progress`, all → `done`) |

`TXXX` is the template's own placeholder and is not imported. Both eras of the template parse
— the current `## Phase 3: User Story 1` grouping and the older `## Phase 3.1: Setup`.
The `## Dependencies` section is prose in both, so nothing machine-readable is taken from it.

#### `kiro` — AWS Kiro specs

`.kiro/specs/<name>/tasks.md`, including the `.kiro/specs/<area>/<name>/tasks.md` nesting real
projects use. One card per spec directory, same shape as spec-kit.

| Kiro | Card |
|---|---|
| `requirements.md`'s `# ` title (or `bugfix.md`, `design.md`) | the title |
| that file's intro | `## Issue` |
| `- [x] 1. Title` and `  - [ ] 1.2 Title` | `## Tasks` items, `1 Title` / `1.2 Title` |
| `- [-]` | an in-progress item |
| `- [ ]* 2.2 …` (optional) | `(optional) 2.2 …` — dropping the mark would lose the meaning |
| unnumbered detail bullets, `_Requirements: 1.1, 2.3_` | not tasks; left in the file |

#### `openspec` — Fission-AI/OpenSpec

`openspec/changes/<change-id>/tasks.md`. One card per change.
`openspec/changes/archive/<YYYY-MM-DD>-<id>/` is finished work kept for the record and is not
imported.

| OpenSpec | Card |
|---|---|
| `proposal.md`'s `# ` title | the title |
| its `## Why` / `## What Changes` | `## Issue` |
| `- [ ] 1.1 Task description` under `## 1. Section` | `## Tasks` items |

**Only `x` or `X` means done here.** OpenSpec's own schema says it in as many words: a box
holding only `x` counts as done, upper or lower case and with any spacing, and every other
marker — `- [~]`, `- [-]`, an empty `- []` — reads as unfinished. That is the *opposite* of
Kiro's `- [-]`, which is why the two have separate line readers.

---

## 5. Never twice

Every item carries a **`source_key`**, `<kind>:<path>#<id>`:

```text
checklist:TODO.md#ed481825
backlog-md:backlog/tasks/task-1 - Show-due-dates.md#TASK-1
beads:.beads/issues.jsonl#bd-a3f2dd
taskmaster:.taskmaster/tasks/tasks.json#master/2
spec-kit:specs/001-user-accounts/tasks.md#001-user-accounts
```

A tracker with real ids uses them. A checklist line has no id, so its id is the **first eight
hex of the digest of its own text** (whitespace-normalised, repeats inside one file
disambiguated by order of appearance). Line numbers would have been the obvious choice and are
the wrong one: inserting a paragraph at the top of a `TODO.md` would move every line and
re-propose the whole file.

After `apply`, the key is recorded twice:

* on the card, in its **`source` front matter field** —
  `source: 'relay-import: "checklist:TODO.md#ed481825" line 11'`. `source` is a legal field for
  every card type and `board_update_card` treats it as immutable provenance, so an agent cannot
  edit the record away. The key is **JSON-quoted** because it carries a path and a path may
  hold spaces: a Backlog.md task file is called `task-1 - Its Title.md`, and a bare-word key
  would read back as `backlog-md:backlog/tasks/task-1` and re-import the task on every run;
* in **`<board>/import-state.json`**, a sorted, committed map of key → `{card, at, kind, path}`,
  so a second clone and a second session agree without reading every card.

`propose` skips a key found in **either**, so losing the state file does not duplicate a card
and deleting a card does not silently re-import it. To import something again on purpose,
remove its key from `import-state.json` *and* delete the card that holds it.

No new front matter field is invented: `relay-board.py check` rejects a key outside
`board.ALLOWED_FIELDS`, which is why the state is a file of its own — and why that file is
JSON at the board root rather than a `*.md` in a category folder, which `Board.card_paths`
would read as a card.

---

## 6. `propose` and `apply`

```python
board_import.propose(project, finding_kinds=None, *, board=None) -> list[Proposal]
board_import.apply(board, proposals, *, tab=None, actor="import", emit=None, when=None) -> list[str]
board_import.skipped_keys(project, finding_kinds=None, *, board=None) -> list[str]
board_import.source_key_of(card) -> str | None
board_import.read_state(board) / write_state(board, imported) / state_path(board)
```

`propose` reads and returns; it writes nothing and, like the probe, opens no socket and runs
no subprocess. `apply` writes, and only through `BoardTools` — the same code the Board
pane writes with — so every card gets an id, a rank, a thread, an undo record and a
`board_changed` event. The per-turn and per-hour ceilings and the duplicate check are the
*agent's* guardrails; an import is the user's own act, so it runs with them off, exactly as the
pane's own writes do.

Three passes, because a card cannot be blocked by one that does not exist yet:

1. create every card, in the order `propose` sorted them;
2. one `board_update_card` per card that needs `## Tasks`, `blocked_by`, `parent` or a field;
3. one thread entry per card saying where it came from, any comments the source carried, then
   the state file.

**The files the import read are never touched.** A `TODO.md`, a `tasks.json` and a `backlog/`
are byte-identical afterwards; `tests/test_board_import.py` hashes every file outside the board
directory before and after each import and compares. Removing converted text from a source is a
separate, explicit action the design reserves for later
(`SWITCHBOARD-DESIGN.md` §7, "Remove converted text from sources") and is not implemented here.

Cards land in the `features` tab when the board has one, else the first folder-backed tab;
`tab=` overrides it and an unknown or filter-only tab is refused before anything is written.

---

## 7. Deliberately not supported

### Jira, Linear, Azure DevOps Boards — link-out only

Not because they are hard to read, but because **there is nothing in the repository to read**.
None of them leaves a tracker file in the checkout: the data is on a server, behind OAuth, and
reaching it would mean a network call and a credential — before the user has agreed to
anything, in a directory nobody has vetted. Both halves of that are against this project's
rules.

What Relay does instead is *notice* them and say so, as a hint, without contacting anything:

* a Jira- or Linear-shaped key in the branch name (`ENG-1204`), read from `.git/HEAD`. The two
  key formats are indistinguishable — `PROJ-123` is both — so the probe reports the pattern it
  matched and `looks_like: "jira-or-linear"` rather than claiming which;
* `.github/ISSUE_TEMPLATE`, which says the project files its issues on GitHub;
* an Azure DevOps remote, which `forge` reports as `azure-devops` — the *git* host is
  detectable even though the *board* is not.

If these are ever supported, they belong behind an explicit, per-service sign-in that happens
after the project is initialized — not in this dialog.

### git-bug

Rare, and its data lives in git refs (`refs/bugs/*`) rather than in the working tree, so
reading it means either running git or reimplementing enough of the object store to walk them.
Neither is worth it for how few projects use it. Revisit if it comes up.

### GitHub and Trello imports

`SWITCHBOARD-DESIGN.md` §7 lists a GitHub export (`gh issue list --json`) and Trello JSON as
importable. Both are files the user produces deliberately, so they are an import of a *file the
user hands over*, not something the probe finds — a different entry point, and not implemented
here.

---

## 8. Protocol (wired 2026-09-18; `AGENT-SESSIONS-PROTOCOL.md` §19.13)

Three request/response pairs on the worker that owns the board
(`board_protocol.BoardCommands`), beside `board_init` (19.12).

### `project_probe` → `project_probe_result`

```jsonc
// GUI -> worker
{"type": "project_probe", "id": 41, "project": "/home/elliott/src/widgetworks"}
// worker -> GUI
{"event": "project_probe_result", "id": 41, ...}     // §3, verbatim
```

Sent when the GUI is about to ask the init question. `project` is **required, absolute and a
directory**; a missing, empty or relative one is an `error`, and the worker never falls back to
its own cwd — the process runs wherever the GUI started it. Safe to send for a project that
already has a board — the answer says so in `board`, and the event then also carries that board's
`root` — and safe to send repeatedly: it writes nothing. `kinds` (optional here too) limits the
run to some of `TRACKER_KINDS`; an unknown kind is an error. This message needs no board.

### `board_import_propose` → `board_import_proposals`

```jsonc
// GUI -> worker
{"type": "board_import_propose", "id": 42,
 "project": "/home/elliott/src/widgetworks",
 "kinds": ["checklist", "backlog-md"]}               // optional; omitted = all
// worker -> GUI
{"event": "board_import_proposals", "id": 42, "root": "/…/board",
 "proposals": [ {"source_key": "checklist:TODO.md#ed481825",
                 "kind": "checklist", "title": "Wrap long lines in the composer",
                 "status": "inbox", "labels": ["imported", "checklist", "todo"],
                 "body": "…", "tasks": [{"text": "…", "status": "open"}],
                 "source": {"kind": "checklist", "path": "TODO.md",
                            "id": "ed481825", "group": "TODO.md", "line": 6},
                 "depends_on": [], "order": 1} ],
 "skipped": 0}                                       // keys already imported
```

The preview. Each proposal is `Proposal.to_dict()`, and `skipped` is
`len(board_import.skipped_keys(project, kinds))` — the items that are in the project and
already on the board, so the dialog can say "23 of 30, 7 already imported". Nothing is written.

### `board_import_apply` → `board_imported`

```jsonc
// GUI -> worker
{"type": "board_import_apply", "id": 43,
 "project": "/home/elliott/src/widgetworks",
 "keys": ["checklist:TODO.md#ed481825", "backlog-md:backlog/tasks/task-1 - X.md#TASK-1"],
 "tab": "features"}                                  // optional
// worker -> GUI
{"event": "board_imported", "id": 43, "root": "/…/board",
 "cards": [{"id": "K7Q2", "source_key": "checklist:TODO.md#ed481825",
            "path": "board/features/2026-09-18-wrap-long-lines.md",
            "status": "inbox"}],
 "skipped": []}                                      // keys that were already imported
```

`keys` is the ticked subset of the last `board_import_proposals`, so the user's choice is what
is written and the worker re-derives the proposals rather than trusting text sent back to it.
Each card row is `{id, source_key, path, status, tab}`, and `skipped` lists the requested keys
that produced no card — already imported, or no longer in the project. A `board_changed` event
follows, as for any other write.

Ordering: `board_import_apply` requires a board **that exists**, so it comes **after**
`board_init`; an uninitialized one answers `error` rather than creating the board on the way
through, which is what a single `board_create` does (19.12). The GUI sends `board_init` (the user
said yes), then `board_import_apply` with whatever was ticked. `project` defaults to the pane's
own board's project and may not name a different one. Like every other write here it goes through
the Board worker's busy guard (`code: "board_busy"`).

All three reply events are desktop-only in `remote/wire.py`: they are a survey of one directory on
this machine, every path in them is local, and they answer a dialog only the desktop shows.

---

## 9. Known limits

* ~~A Task Master subtask's dependencies are not written.~~ **Fixed 2026-09-18.**
  `board_update_card`'s `tasks` now takes `blocked_by` per item (`SWITCHBOARD-FORMAT.md` §2.5):
  a number is the 1-based position of another item in the same call, which is how a list that has
  no ids yet names itself; a string is an item id already on the card, or `#K7Q2`. The importer
  uses it, so a subtask that waits for a sibling gets a `blocked_by=` marker, and one that waits
  for another *task* is blocked by that task's card — a card is the nearest the format can say,
  since an item cannot name an item of another card. A reference to nothing, a self-reference and
  a cycle are all refused, because `check` calls all three errors.
* **spec-kit, Kiro and OpenSpec import one card per directory**, capped at 100 checklist items;
  a spec with more says so in its body and the rest stay in the file. If a card per phase turns
  out to be what people want, that is a product decision, not a parser change.
* **Kiro's task grammar is observed, not documented** — see below.

### Formats that could not be verified from a primary source

Everything else in §4 comes from the project's own documentation or source, cited in the parser
docstring. These three do not, and the code is deliberately lenient about them:

1. **Kiro's `tasks.md` line grammar.** `kiro.dev` documents that spec tasks exist, are
   "discrete, trackable" and can be optional, but publishes no line format anywhere — not in
   the current docs and not in the archived 2025 concepts page. The grammar here (`- [x] 1.`,
   `- [ ] 1.2`, `- [ ]*` for optional, `- [-]` for in-progress, the trailing
   `_Requirements: 1.1, 2.3_`) was taken from real Kiro-generated artifacts, including ones in
   AWS's own `kirodotdev/Kiro` repository. Treat it as observed and stable, not contractual.
   The reader skips a line it does not recognise rather than guessing at it.
2. **Task Master's string ids and string dependencies.** The docs say `id` is a number. Real
   files — including Task Master's own — hold both integers and strings, and dependencies hold
   both `12` and `"21.4"`. Parsed defensively, both ways.
3. **Beads' `_schema` header line.** Its importer skips such a line; no code in the project was
   found that *writes* one. Treated as optional and skipped if present.
