# The Board as the project's manager: objects, links and tabs (design, 2026-09-25)

Card #EA37; builds on #9FX8. While #9FX8 (Cards | Skills | Memories) was under way the owner
widened the frame: "the board could be a broader project manager: cards, pane list (to compare
with sessions, maybe not needed), artifacts (maybe too redundant with files), skills, memories,
and links among them." This page is the design that answers that frame for **one project**, so
that #9FX8's tab strip is built as the first three of a set rather than a special case. The
portfolio layer *above* projects is a different page, `docs/GLOBAL-PROJECT-BOARD-RESEARCH.md`
(#V3R3); the tooling hub (tests, profiling, hotspots) is `docs/BOARD-TOOLING-RESEARCH.md`
(#7BM4); the object vocabulary (cards build servers, servers serve cases) is card #1QKM and its
children #FVVY, #G9ZD, #GW74, #4YKJ.

Three sourced research passes were run for this page and are kept whole under
[`research/project-board/`](research/project-board/): (a) [how single-project tools hold several
object kinds and link them](research/project-board/a-single-project-object-models.md) (a GitHub
repository, Trello, Notion, Linear, Basecamp, Jira + Confluence, Fibery, Airtable, Plane,
Height); (b) [what agent-era tools keep per project](research/project-board/b-agent-era-project-state.md)
(Claude Code, Codex, Cursor, Devin, OpenHands, Copilot, Windsurf, Cline, Amp, Jules, Kiro, Beads,
git-bug, Backlog.md, MLflow, W&B, DVC, Sacred, Hydra, Zenodo, Quarto); (c) [link models and
read-time backlinks](research/project-board/c-link-models-and-backlinks.md) (Obsidian, GitHub,
Linear, Notion, org-roam, Beads, Jira, Logseq, Dendron, Foam, Zettelkasten, Tana, Anytype,
Trello). §9 says what each added; a claim below with a product in brackets is sourced there.
Nothing on this page is implemented unless a card says so.

## 1. Where this sits, and which shape the Board already has

Three layers, three pages:

| Layer | Question | Page / card |
|---|---|---|
| Portfolio | which projects, how critical, what budget | `GLOBAL-PROJECT-BOARD-RESEARCH.md`, #V3R3 |
| **Project** | **what does this project consist of, and how do its parts point at each other** | **this page**, #EA37, #9FX8 |
| Card | what is being built, is it done | `BOARD-DESIGN.md`, `BOARD-FORMAT.md` |

The per-project layer is the one every tracker the owner named is built for, and the research
(pass a) shows they take three shapes:

- **Trello: one object kind.** The card is the hub; checklists, attachments, card-to-card links and
  Power-Up sections hang off its back. Links are untyped attachments; nothing is computed.
- **Notion / Fibery / Airtable: one database per kind, typed relations between.** The project page
  is assembled from filtered views; relations are one-way by default and two-way only when one
  server writes both ends.
- **A GitHub repository: a fixed set of object kinds as tabs**, one number space, links by stable
  id in prose (`#123`, `owner/repo@sha`, `path@sha#L10`), and the reverse direction *computed*
  from the text into an append-only event on the target's timeline. Procedures (issue forms,
  reusable workflows) are versioned files in the repo, referenced by ref.

Relay's Board is already the third shape — a folder of files with content-stable ids, `#K7Q2` in
prose and commit messages, skills as `SKILL.md` folders — and every agent-era tool in pass b is
migrating *toward* it ("durable knowledge = committed files; volatile state = app": Devin retired
app-held Knowledge for repo skills, Windsurf tells users to move memories into Rules, Cursor
removed Memories). This page keeps the Board in that shape and adds what the repository model has
and the Board lacks: the computed reverse direction, and a tab per kind that a person manages.

## 2. What already exists, object by object

Ground truth as of 2026-09-25 (`docs/BOARD-FORMAT.md` §1–2, `docs/ARCHITECTURE.md` §10a,
protocol §19.22, the cards named):

| Object | Store today | Surface today | Links it carries today |
|---|---|---|---|
| Card (work) | `.board/<category>/…md`, thread in `threads/<ID>.md` | Board list page, card page, card pane (#Y2BA) | `links: {plans, commits, evidence, related, github}`, `blocked_by`, `parent`, `session` (pane token), `links.signal`, `## Tasks` `card=` markers, `#ID` in prose |
| Memory (project) | `.board/memory/<name>.md` (+ `archive/`, `.private/memory/`); a memory card **has a four-character id like a work card** | Globals' "All records" — wrongly, per the owner; #9FX8 moves it to the Board's Memories tab | `paths` (globs that auto-attach it), `supersedes`, `pinned`, `scope`, `reviewed` |
| Skill (project) | `.relay/skills/<id>/SKILL.md`, workspace `.claude`/`.codex`/`.warp` | `SkillsDialog` (manager off the pane); #9FX8 step 1 landed `skills_registry` | `profile:` block; none to cards — a card names its server (`server:`, #G9ZD) |
| Alias | `.board/aliases/`, global aliases | Globals; `/name` | `kind`, `## Run` |
| Case | `.board/cases.jsonl`, append-only | none by design (agent-facing, owner steer 2026-09-23); the skill page will list them | `server`, `server_version`, `card`, `input`, `served_by` |
| Run and artifact | planned: `runs.jsonl` (#FVVY steps 2–3) | none | `card`, `case`, `outputs[{path, sha256}]`, `superseded_by`; artifact address `run:<id>#<path>` |
| Evidence | `docs/qa_evidence/<date>-<id>-…/` | `links.evidence` on the card | path only |
| Test | `## Tests` on the card; the tool row's Tests/Check (#7BM4) | card page | test id ↔ card |
| Signal | machine store (`BOARD-FORMAT.md` §5.1) | one row of the list (#AQ6X) | `links.signal` |
| Pane / session | live process state; `state/projects.json`; conversation index | Sessions & Projects (Ctrl+Shift+S): the Projects page lists live panes per project incl. "No project"; the Sessions page lists conversations | a card's `session`; thread entries' `pane_token` |
| File | the working tree | file panes, artifact workspace (#P2W8) | memory `paths`; card `links.evidence`; `run.outputs` |

Two facts shape everything below. **Every link is stored once, forward, on one side**, and
**nothing computes the reverse**: no card knows which memories mention it, no skill knows its
cards except through ledger rows, no memory knows which cards touched the files its `paths`
cover. And **the worker already re-reads the whole board on every write** (the
`QFileSystemWatcher` → `board_refresh` path), so a computed reverse index costs one pass it is
already making.

## 3. The rule for a tab

Not every object kind gets a tab. #1QKM §4 gave the test for "needs its own tooling": durable, has
a lifecycle, shared across cards, goes wrong when implicit. A **tab** needs one more thing: **a
person acts on the kind as a set** — creates, retires, verifies, compares, prunes. If the only
thing a person ever does with an object is *look at it from something else*, it is a panel on
that something else, not a tab. Applied:

| Kind | Durable, lifecycle, shared, harmful when implicit | A person acts on the set | Verdict |
|---|---|---|---|
| Cards | yes | yes (the whole Board) | **tab** (#9FX8, exists) |
| Skills | yes: version, staleness, exclusion, re-verify | yes: prune duplicates, re-verify, exclude, refine | **tab** (#9FX8) |
| Memories | yes: active/retired, supersedes, review | yes: retire, pin, correct, review suggestions, re-verify expired ones | **tab** (#9FX8) |
| Artifacts | yes once runs exist: versions chain, canonical vs superseded, location | yes: mark canonical, compare two figures, find the run behind a PDF | **tab, later** — gated on #FVVY's `runs.jsonl`; until then `links.artifacts` on the card |
| Cases | yes | no — the owner's steer: agent-facing, not drawn | **no tab**: rows on the skill page and the card page |
| Panes / sessions | live, not durable | the Projects page already does it (attach, reveal, filter sessions) | **no tab**: a **Live** strip on the Board (§6) |
| Tests, signals, hygiene | durable | yes, but through the tool row (#7BM4, #AQ6X) | stay in the tool row |
| Files | the tree | yes, in file panes | **no**; "artifacts" is the subset that came *out of a run*, which is what makes it not redundant with files (§5) |
| Aliases | yes | rarely; Globals covers it | no project tab; a `/` picker |

So the strip is **Cards | Skills | Memories** now and **Cards | Skills | Memories | Artifacts** when
runs exist. The control is a segmented strip that accepts a fourth entry without a redesign; tab
labels carry counts (`Skills 7`, `Memories 12 · 2 suggested`); Cards opens first (owner decision
1 on #9FX8); a card pane (#P2W8/#Y2BA) never carries the strip. `board.yaml`'s `tabs:` stay what
they are — the Cards list's categories — and are not confused with object tabs (the 2026-09-18
"no tabs" decision, `BOARD-DESIGN.md` §4.6, was about categories and still stands inside Cards).
This is the GitHub repository's tab row (Issues, Pull requests, Discussions, Wiki, Releases…),
not Trello's board or Notion's page: one number space, one kind per tab, one id syntax.

## 4. Links: stored once, forward; reverse computed; a small typed vocabulary

The evidence in pass c is one-sided. Every file-based system stores forward links only and
computes the reverse (Obsidian's backlinks pane, org-roam's rebuildable database, Logseq,
Dendron, Foam, Anytype). Stored two-way links exist only where one server writes both ends in
one transaction (Notion "Show on", Linear, Jira's one edge with two names), and the stored
reciprocal that crosses a boundary is the one Atlassian documents as drifting with "root cause
unknown" (Confluence → Jira "Mentioned in"). Two files in git each holding one end of a link
drift the first time two agents edit them in the same window — which on this repo is every day.

### 4.1 Addresses

One address form per kind, usable in front matter, in prose, in the filter box and in a commit
message, and greppable with `rg --hidden` when Relay is not running. Every system that scales
past one kind uses **a sigil or prefix on the same token shape**, not a new syntax (GitHub `#26`
/ `owner/repo#26` / `owner/repo@sha`; org `id:` / `file:` / `doi:`):

| Kind | Address | Example | Stable because |
|---|---|---|---|
| Card, memory card | `#ID` | `#K7Q2`, `#ANRZ` | four-character content-stable id; memory cards already share the id space, so no `memory:` prefix is needed |
| Skill | `skill:<id>`, `skill:<id>@sha256:…` when a version is pinned | `skill:referee-review-support` | id is the folder; the digest is what `cases.jsonl` already records as `server_version` |
| Case | `case:<id>` | `case:c-8f3a2b1c` | ledger id, append-only |
| Run | `run:<id>` | `run:r-3e9d1a20` | ledger id (#FVVY) |
| Artifact from a run | `run:<id>#<path>` | `run:r-3e9d1a20#figures/fig2.pdf` | #FVVY step 3 |
| File in git, pinned | `<path>@<sha>`, `<path>:<line>@<sha>` | `src/BoardPane.cpp:3901@e5a0bc03` | GitHub's permalink rule: "the version of a file at the head of branch can change" |
| Commit | 7–40 hex | `e5a0bc03` | immutable |
| External | URL | `https://…` | — |
| Pane | `pane:<token8>` — **display only, never a stored link target** | `pane:cca5043b` | a pane is a moving target; store what it produced (a case, a commit, an artifact). The card's `session` field is the one exception and is already dropped when the pane closes |

Ids, never titles or paths, for anything Relay owns: a card keeps its file name forever
(`BOARD-FORMAT.md` §1); title-based links (Obsidian, Dendron, Foam) depend on the editor
rewriting them on rename, and a `git mv` by an agent dangles them in a way that looks identical
to a link to a not-yet-written note. A bare path is a valid address only for a *file*, where the
path is the identity, and it is pinned with `@sha` wherever the content matters.

### 4.2 Stored forward links: field-name-is-the-type

The typed vocabulary that users actually fill converged across GitHub (2025 sub-issues and
dependencies), Linear, Jira's defaults and Beads' blocking class: **blocks / blocked by, parent,
duplicate, related**, plus one that agents need, Beads' **discovered-from**. Everything beyond
that is drift: Beads grew to ten types and had to document that six are "graph annotations
only"; Jira admins add `causes`, `reviews`, `implements` and nobody fills them. The types that
survive are the ones that *do* something: block scheduling, roll up to a parent, close on
duplicate, close on merge. Relay's front matter already encodes its edges as **fields whose name
is the type** (the Obsidian-properties / Tana-field model), so the vocabulary is a small fixed set
of keys, not a free relation picker:

| Relation | Stored on | Field | Exists | Does something |
|---|---|---|---|---|
| related | card | `links.related` | yes | the untyped default; a `#ID` mention in prose is already this (Linear derives `related` from mentions) |
| blocks / blocked by | card | `blocked_by` | yes | readiness; the Live strip and the list badge |
| parent / child | card | `parent` | yes | `☑ n/m` roll-up; split/merge |
| duplicate of | card | `duplicate_of` (single id) | **new** | setting it closes the card `dropped` with the pointer, as Linear and GitHub do |
| discovered from | card | `discovered_from` (single id) | **new** | written by the deliver flow when an agent files a card mid-turn on another card; provenance for the "unrelated faults become a card" rule |
| supersedes | memory card; **work card** | `supersedes` | memory yes; card **new** | a rewrite of a request or plan; the old card closes to it |
| builds (card → server) | card | `server:` (skill id or program path) | #G9ZD | the skill page's card list; #G9ZD's fail → card loop |
| serves (case → server, card) | ledger row | `server`, `card` | yes | pass rate, staleness, the third-ad-hoc-case hint |
| evidence, commits, plans, github | card | `links.*` | yes | the verifier's inputs; worker-filled |
| produced (run → artifact), for (run → card, case) | run row | `outputs`, `card`, `case` | #FVVY | the Artifacts tab; Canonical |
| applies to (memory → files) | memory | `paths` | yes | auto-attach on read (Cursor `.mdc` globs, Copilot `applyTo`, OpenHands `triggers` are the same mechanism) |
| held by (card → pane) | card | `session` | yes | claim; dropped on release |
| counterparty | card, case row | `counterparty` | #FVVY | filter; confidential rule |
| **mention** (untyped) | any prose or thread | `#ID`, `skill:x`, `case:c-…` in text | `#ID` yes; the prefixes are new | the computed backlink (§4.3) |

Nothing here adds a second copy of a link on the other side. A skill's "cards" are the cards whose
`server:` names it plus the ledger rows that name both; a memory's "cards" are the cards whose
text mentions it plus the cards a turn wrote while the memory was attached (the turn's cases name
the card). Edges also **carry lifecycle** (pass a: Linear moves *blocks* to *related* when the
blocker resolves; GitHub closes the issue when the `closes` PR merges): a `blocked_by` whose
target is done stops badging; a `duplicate_of` closes; `supersedes` retires.

### 4.3 The computed reverse: `board_links`, plus one append-only line

The worker builds an in-memory **link index** on every board load and refresh — a *function of the
files*, recomputed whole, never patched per event (Logseq #7362 dropped every block reference to
a file when Dropbox rewrote it, because its index treated a reload as a delete). For every address
it sees (the fields above, `#ID` and prefixed mentions in card bodies and threads, ledger rows,
run rows, skill `profile:` and `## Tests`), it holds `(from, relation, to, where)` edges. It is
never written to disk and never committed — the org-roam rule ("the notes are still functional
even if Org-roam ceases to exist") and the Beads rule ("`issues.jsonl` is an export, not the
source of truth"); a committed index is the drift the portfolio research already recorded.

**The one stored reverse worth having is GitHub's shape**: when a write mentions `#K7Q2` from
another object, the worker appends one line to `threads/K7Q2.md` — `mentioned in #EA37 · 2026-09-25
· codex` — an append-only text line that merges as an append (`merge=union`, like every thread
entry), doubles as the `rg`-readable backlink when Relay is not running, and is the only stored
reverse form that cannot drift because it is never edited. If a thread line and the index
disagree, the index wins, because it was computed from the forward links. Generated reverse data
never goes *into a card* (Foam's link-reference footer is off by default "to reduce clutter"; a
generated section in a file several agents edit is a merge-conflict factory).

`board_links {address}` (new request; the file fallback is `rg --hidden '<address>' .board/`)
answers both directions grouped by kind with the place each edge was read from, so a page can draw:

- **Card page › Linked**: server (skill/program), memories mentioned or attached, cases (n, pass
  rate), runs and artifacts, commits, evidence, cards that mention it (backlinks), blocked-by /
  blocks, parent / children, duplicates, what it superseded, what discovered it. Trello's card
  back and Linear's issue sidebar are the shape: the hub object is the card and every link is a
  section of its page.
- **Skill page › Linked**: cards that build it (`server:`), cards its cases name, memories and
  threads that mention it, its last ten cases (#9FX8 step 2), sibling skills with the same
  content hash (#SZ1H).
- **Memory page › Linked**: cards and threads that mention it, files its `paths` cover (with the
  newest commit touching them, so an expired memory is visible), the card or turn that wrote it
  (`source`), what it supersedes and what superseded it.
- **Artifact page › Linked**: the run, its card and case, the commit and whether the tree was
  dirty, the predecessor it superseded, the file panes that have it open.

The same index serves the filter box across tabs: `links:#K7Q2` (anything pointing at the card),
`skill:x` on the Cards tab (cards building or served by that skill), `case:`, `run:`; and
`relay-board.py check` gains `dangling_link` (an address that names nothing) beside
`dangling_section`, with an id collision on merge an error rather than an alphabetical pick
(Foam's documented resolution).

### 4.4 Why not a graph view

Every plaintext system with a graph view ships one and every write-up of them says the same: "the
full graph has become completely useless as the number of notes has grown" (Obsidian forum);
Obsidian added an "existing files only" filter because dangling links clutter it. The useful
shape is the **Linked panel on the object** and a **filter that follows a link** — GitHub's
timeline and Obsidian's backlinks pane, not their graph tabs. Nothing vault-wide is planned; at
most a depth-1 neighbourhood of one card, which is what the Linked panel already is.

## 5. Artifacts are not files: what makes the fourth tab worth having

The owner's doubt — "artifacts (maybe too redundant with files)" — is right for files in general
and wrong for the subset that came out of a run. The report behind #1QKM found the failure in the
owner's own trees: results with no run id, 549 PDFs with no commit, figures overwritten per run.
A file pane answers "what is in this file"; the Artifacts tab answers **"where did this come from,
what did it replace, and is it the one we sent"**. Pass b supplies the two mechanisms:

- **Append-only versions with a mutable pointer on top** (W&B artifact aliases, MLflow
  `@champion`, Zenodo's concept DOI over versioned DOIs). Versions never change; the pointer
  moves. The tab's one action is **Canonical**: mark this version the one the card's Done means
  refers to, written as `links.artifacts` on the card (the pointer); #FVVY's `superseded_by` is
  the version chain underneath.
- **The run record that answers "can I reproduce this figure"** is small and published (Sacred's
  `run.json`: command, status, times, git commit **and a `dirty` flag**, content-hashed source
  copies, host; Hydra writes the composed config into the run directory). #FVVY's row has
  everything but `dirty`, and should add it: a run from a dirty tree is the one that cannot be
  reproduced, and nobody else flags it.

The row is `path · run · card · commit (dirty?) · host · superseded?`; the page shows the chain of
versions of that path, the run's config and cost, Open / Compare / Canonical, and **Make a skill**
(Claude Code's `/run-skill-generator` and Devin's session → skill suggestion: a run that worked
becomes a server, the #G9ZD third-ad-hoc-case hint in button form). Everything else about the
file (open, preview, diff) is the file pane's. A run row is never deleted, only superseded (W&B's
documented trap: deleting a run deletes its artifacts). The tab exists only once `runs.jsonl`
does; until then the card page shows `links.artifacts` as plain paths.

## 6. Panes and sessions: a Live strip, not a tab

"Pane list (to compare with sessions, maybe not needed)" — not needed as a tab. The Projects page
of Sessions & Projects already lists a project's live panes (and the panes on no project) and is
where attach, reveal and filter live (#P7SJ, #SPSG). What the Board lacks is the *reverse*: from
the Board, who is working here right now. That is one **Live** strip at the top of the Cards
tab, computed and never stored: one chip per pane attached to this project — its first-eight
token, its model, the card it holds (`session` on a card in `executing`), and ✦ while a turn
runs — each chip revealing the pane on click, the same link the card row already draws from
`session`. Sessions (past conversations) stay in the Sessions page; a card's thread already links
the pane tokens that touched it; and a pane is never a stored link target (§4.1). Nothing new is
written for this.

## 7. The pages: one shape for every tab

Each tab is the same page family the Cards tab already is, so the Board reads as one surface:

- **List**: the filter box at the top of the page (§4.7 of `BOARD-DESIGN.md`), rows with a fixed
  id column and right-aligned badges, sections with counts, a `+` where creating makes sense
  (Cards, Memories; Skills creates through Refine/import, not a blank row). The Memories list is
  drawn from front matter only — never loading bodies to draw rows (Cline's memory bank re-reads
  six files per update and its own docs say to start small).
- **Page** (the right half, or a pane of its own): head, the object's own words, its **Linked**
  panel (§4.3), its history (a card's thread; a skill's changelog and cases; a memory's `LOG.md`
  entries), and an action row specific to the kind — Cards: Discuss / Plan / Run / Verify; Skills:
  Load / Exclude / Refine / Open file / Re-verify / Try it; Memories: Edit / Retire / Pin /
  Re-verify; Artifacts: Open / Compare / Canonical / Make a skill.
- **Freshness on memories** (pass a: Notion's wiki verification — owner, expiry, re-verify
  notification — is the one knowledge-freshness contract in the field, and it rots for most
  teams because it is a paid tier). Memory cards already carry `reviewed`; the Memories tab shows
  its age against the card's `paths`' newest commit, lists **expired** memories first under their
  own section header, and Re-verify is one click. Cheap enough to be on by default is the point.
- **Console**: the page's agent console at the bottom with a context for the object (the Cards
  list's `BoardContext`, a card's `CardContext`; a `SkillContext` and `MemoryContext` follow the
  same `relay::agent::Context` shape and carry the object's address so the agent's `board_*`
  writes land on it).

Keyboard: the strip is Ctrl+PgUp/PgDn (the keys `BOARD-DESIGN.md` §4.4 reserved for tabs and
that the no-tabs decision freed), `1`–`4` when the list has focus; everything inside a tab is the
Cards tab's existing map.

Not planned: a project **home page** in the GitHub sense (README plus a sidebar digest: latest
release, expired memories, open cards by status). The owner decided Cards opens first; the counts
in the tab labels and the expired section on Memories carry the digest's useful part. Listed in
§10 in case the owner wants it once four tabs exist.

## 8. Phasing: what lands on which card

| Phase | What | Card |
|---|---|---|
| 1 | The strip (Cards \| Skills \| Memories), the skill page, the Memories tab with the expired section, Globals › Skills; **the strip built for a fourth entry**; the card page's and skill page's Linked panels computed from what exists now (`server:`, ledger rows, `paths`, `#ID` mentions) | #9FX8 steps 2–4 (executing) |
| 2 | `board_links` as a request and the in-memory index; the `mentioned in` thread line; `skill:` / `case:` / `links:` in the filter language; `duplicate_of`, `discovered_from`, `supersedes` on work cards with their behaviours; `dangling_link` in `check`; memory page Linked | new card |
| 3 | The Live strip on the Cards tab | new card, small |
| 4 | `runs.jsonl` **with a `dirty` flag**, artifact addresses, `counterparty`, `schedule:` | #FVVY (amended) |
| 5 | The Artifacts tab, Canonical, the version chain page, Make a skill | new card after #FVVY |
| 6 | The portfolio page reads the same computed index for its rollups (open cards, stale skills, expired memories, last run per project) instead of a second aggregation | #V3R3 |

Phase 1 is the only change to #9FX8: the segmented control accepts a fourth tab, the Memories
tab gets its expired section, and the two Linked panels are drawn from the data step 1 already
returns (`cards`, `cases` on the registry row) plus the card's own front matter. Neither needs
the index; the index (phase 2) replaces their per-page computation with one shared one.

## 9. What the research passes add

Each item names its pass and the products that show it; the sources are in the pass.

### 9a. Single-project object models (pass a)

1. **Mentions become edges on the target, computed from text** (GitHub `cross-referenced`
   timeline events; Linear turns a mention into *Related*; Jira/Confluence promote a pasted URL to
   a stored link). §4.3's index and thread line.
2. **A small closed vocabulary of typed, directional edges, stored once and rendered from both
   ends with different labels** (Jira inward/outward names; Linear's four; GitHub's parent,
   blocked-by, closes, duplicate; Plane's three plus custom). §4.2.
3. **Edges have lifecycle** (Linear: *blocks* → *related* on resolve; GitHub: close on merge).
   §4.2's last paragraph.
4. **Procedures are versioned files in the repo** (GitHub issue forms and reusable workflows
   called by `@ref`); every other product stores playbooks in its own database and Trello meters
   automation runs per workspace. Skills as `SKILL.md` folders are the same model; a case pins
   the version (`skill:x@sha256:…`).
5. **Artifact links are content-addressed** (GitHub permalinks `path@sha#L10-L20`), and the trap
   beside it: autolinks and permalinks work only in conversations, "not in Markdown files" — a
   file-based board must resolve addresses in *every* file it owns, or people learn that links
   only work in some places. §4.1 and `check`'s `dangling_link`.
6. **Progress is a rollup over a typed child edge, never a stored number** (Notion completion,
   Linear's project graph, GitHub sub-issue progress). The `☑ n/m` badge already is; project
   health for #V3R3 must be too.
7. **Knowledge with a freshness contract** (Notion wiki verification: owner, expiry, re-verify).
   §7's expired section on Memories.
8. **Status is a dated stream** (Linear project updates, newest on the overview, interleaved with
   property diffs). `cases.jsonl` and threads already have this shape.
9. **The hub object is the card and links are sections of its page** (Trello's card back, Linear's
   sidebar). §4.3's Card page › Linked.
10. **Copies and mirrors must say what they left out** (Trello board copies drop comments and
    activity; mirrors cannot run Power-Ups and are not archived with the source; Basecamp
    templates reset to-dos and shift relative dates). A future "new project from this one" strips
    threads and ledgers and says so; a card shown on another board is a reference with an origin
    badge, never a duplicated file (#V3R3's rule, confirmed).
11. **Custody**: Height announced in March 2025 and closed on 2025-09-24; everything lived in its
    database. Git-native files mitigate only if the *links* are in the files too — which §4.2's
    forward fields and §4.3's thread line guarantee, and the uncommitted index does not need to.

### 9b. Agent-era project state (pass b)

1. **Every vendor is converging on "durable knowledge = committed files, volatile state = app"**,
   and the ones that put knowledge in the app are migrating it out (Devin's Knowledge → repo
   skills "because it could drift"; Windsurf memories → Rules; Cursor's Memories and Notepads
   pages removed). The Board's bet is what they are moving toward. The one thing this page keeps
   *out* of git — the link index — is volatile by nature and regenerable in one pass, which is the
   Beads rule.
2. **One file per memory with a modified timestamp and a size cap, and an escalation ladder when
   the cap is hit** (Claude Code auto-memory: warn → suggest compaction → refuse the write; Codex
   `memories.jsonl` deduped on write). Relay's memory cards have the shape; the Memories tab shows
   age and a count against a cap rather than trimming silently.
3. **Per-path activation by front-matter globs** (Cursor `.mdc`, Copilot `applyTo`, OpenHands
   `triggers`) is Relay's memory `paths`, which makes `paths` a link worth drawing and the basis
   of the expiry check (§7).
4. **The reproducible run record** (Sacred's `run.json` with commit **and `dirty`**, hashed
   sources, host; Hydra's config in the run directory). #FVVY adds `dirty` (§5).
5. **Append-only versions with a mutable pointer** (W&B aliases, MLflow `@champion`, Zenodo
   concept DOI). §5's Canonical.
6. **Turn a successful run into a skill with one action** (Claude Code `/run-skill-generator`,
   Devin's session → skill). §5's Make a skill.
7. **Hash ids to survive branches and multi-agent merges** (Beads `bd-a1b2`; it moved from SQLite
   to Dolt for real merges and warns that its JSONL "is not the source of truth"). The Board has
   the ids; the pass adds Beads' schema-version guard ("database is at v45, binary knows up to
   v42") as the shape for `board.yaml version:` when it next moves.
8. **Typed relations beat "see also" prose** (Beads `relates-to` / `duplicates` / `supersedes` /
   `replies-to`; Zenodo `related_identifiers`; Copilot issue → PR). §4.2's `duplicate_of`,
   `discovered_from`, `supersedes` on work cards.
9. **Traps**: cascade deletes from app state into files (a run row is never deleted); memory bloat
   by full re-read (the Memories list draws from front matter only); secrets pasted into committed
   context files (Devin's DeepWiki docs repeat "do not store API keys"; the Memories tab says
   where secrets never live, which Relay's key store already enforces).

### 9c. Link models and backlinks (pass c)

1. **Forward links only in files; the reverse is computed** — every file-based system, no
   exceptions; two-way stored links exist only under one writer; the cross-boundary reciprocal
   drifts (Atlassian's KB). §4 as a whole.
2. **GitHub's append-only cross-reference event is the one stored reverse that merges in git**
   because it is a log line, never edited. §4.3's `mentioned in` thread line.
3. **The typed set that survives is four plus one**: blocks, parent, duplicate, related, and
   Beads' discovered-from; Beads' ten types and Jira's admin-defined ones are the cautionary
   tales. §4.2.
4. **Types get filled only when they do something** (close on `Fixes #10`, readiness in `bd
   ready`, roll-up, auto-close children); Linear derives `related` from any mention. §4.2's
   "does something" column.
5. **Non-file targets use a sigil on the same token shape** (`owner/repo#26` vs `owner/repo@sha`;
   org `id:` / `file:` / `doi:`; permalinks are path + commit). §4.1.
6. **Never link to a live thing** (permalinks exist because a branch head moves; a pane is the
   same kind of target). §4.1's pane row and §6.
7. **Title-based links break on rename outside the app; ids assigned to the object survive**
   (Dendron's never-edited front-matter `id`, org `:ID:`, Beads hash ids). Memory cards already
   have four-character ids, so `#ID` addresses them and no `memory:` prefix is needed — a
   correction to this page's first draft.
8. **Indexes must be a function of the files** (Logseq #7362; Beads' "upsert-only import cannot
   infer deletion"). §4.3's whole-recompute rule.
9. **Graph views past depth 1 are decoration** (Obsidian forum; the "existing files only" filter).
   §4.4.
10. **Generated reverse data never goes into the card** (Foam's footer off by default). §4.3.

## 10. Decisions for the owner

1. **Artifacts as a fourth tab, gated on #FVVY** (recommended), or fold artifacts into the card
   page only and never give them a tab?
2. **Cases stay undrawn as a set** (recommended: rows on the skill and card pages only), or a
   Cases tab once a non-software pilot (#FVVY step 5) shows a person wants to browse them?
3. **Live strip on the Cards tab** (recommended) versus nothing on the Board and the Projects
   page alone?
4. **The three new card fields with their behaviours** — `duplicate_of` closes the card,
   `discovered_from` is written by the deliver flow, `supersedes` retires the old card
   (recommended: all three, in phase 2) — or `related` stays the only free link on a card?
5. **The `mentioned in` thread line** (recommended: it is the `rg`-readable backlink and the one
   stored reverse that cannot drift) — or the index alone, with no line written?
6. **Phase 2 as its own card now** (recommended, so #9FX8's Linked panels are built against the
   request shape), or wait until the Skills and Memories tabs have been used for a week?
7. **A project home page** (README head plus a computed digest) — not recommended now; the tab
   counts and the expired-memories section carry it. Revisit at four tabs.
