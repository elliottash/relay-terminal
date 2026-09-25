# A global project board for Relay (research and product proposal, 2026-09-24)

Card #V3R3. Relay needs a global Projects feature above per-project Boards: users can mark
critical projects, set optional time budgets, and track work that spans projects. Existing
organizing systems were used only as design examples. This is a product feature for Relay users;
it is not a request to build or migrate a personal board.

Three sourced research passes are kept whole under
[`research/global-project-board/`](research/global-project-board/): (a) [the portfolio layer in
fourteen products](research/global-project-board/a-products-portfolio-layer.md) (Linear, Jira,
Asana, Notion, GitHub Projects, Trello, Height, Plane, Focalboard, Basecamp, ClickUp, monday,
Shortcut, Airtable); (b) [plain-text, git-native and CLI
systems](research/global-project-board/b-plaintext-and-git-native.md) (Taskwarrior, org-mode,
Obsidian, Logseq, Beads, git-bug, git-issue, dstask, todo.txt, gh, Johnny.Decimal, PARA, Dendron,
Foam); (c) [prioritisation, time and agent budgets, and how academics track a paper
pipeline](research/global-project-board/c-priority-budgets-and-research-pipelines.md). A fourth
pass examined the example structures; its named data is kept outside this repository. This page is a product proposal, not an implementation claim.

## 1. What the example structures reveal

The user's existing systems were examined as design examples. They show three different needs:
a short-horizon action board, a portfolio table with a domain-specific workflow, and one folder
per project containing notes and links. These shapes should inform Relay's feature without
becoming its schema, default data, or required import path. The private inventory and matching
notes remain outside this repository.

The general failure mode is that manually maintained fields become stale when no regular
workflow reads them. Relay can help by showing linked Board and session activity next to explicit
project priorities and updates. It must also represent projects with no local repository or
Board, and it must not infer that silence means a project is inactive.

## 2. What the research agrees on

Fourteen products and fourteen file-based systems, read independently, converge on a short list.
The pass letter and product are in brackets; the sources are in the reports.

1. **The layer above a project is a thin wrapper with its own fields, never a copy of the cards**
   (a: Linear Initiatives, Shortcut Objectives, GitHub Projects rows). GitHub Projects is the
   cleanest statement: a row *references* an issue in a repository and the project owns only its
   overlay fields (status, priority, iteration). Everything that copied cards upward — Trello
   Butler mirrors, monday Connect+Mirror columns — became the product's best-known drift complaint.
2. **Aggregation is computed at read time; a committed index drifts** (b: org agenda, Obsidian
   Dataview and Bases, Logseq, Beads). Beads had to write into its docs that its JSONL export is
   "not the source of truth". A hand-maintained project index has the same failure mode.
3. **Status, health and progress are three fields** (a: Linear, Asana). Status is where the project
   is in its lifecycle; health is a human's dated opinion (on track, at risk, off track); progress
   is computed from the board. Collapsing them produces the sheet's `active = 0.5`.
4. **A dated, append-only project update with a mandatory health pick** (a: Linear project updates,
   Asana status updates) is the sheet's Last Meeting column done right: it lives in the thread,
   the latest one is what the board shows, and its age is the staleness signal.
5. **Portfolio priority is its own small integer, and the critical set is capped** (a: Linear gave
   projects a priority in 2024, independent of issue priority; Basecamp's Lineup is an ordered
   short list; c: Doerr's 3–5 objectives, Burkeman's closed list, Newport's one or two big
   projects). Every source that thought about it caps the top band; none lets "critical" inflate.
6. **Per-project weight beats per-item ranking** (b: Taskwarrior's
   `urgency.user.project.<name>.coefficient`): raise a project's coefficient and every item in it
   outranks the others, with no item touched. Relay's card `priority` and a project criticality
   compose the same way.
7. **Time budgets are hours per week per project against a person's capacity, with over and under
   shown** (a: Asana Workload, ClickUp Workload, Jira capacity plans; c: Toggl and Clockify budgets
   with 50/80/100 % alerts; Silvia's defended writing hours). Budgets that are never metered
   become decoration (c, Traps), so the actuals feed matters more than the budget field.
8. **A "stuck projects" report is the cheapest health check** (b: org-mode `org-stuck-projects`:
   a project with no next action; c: flow metrics' work-item age; GTD's Waiting For for
   ball-in-court). It is more honest than a manually maintained priority column.
9. **Content-stable ids and a config-file registry of roots** (b: Beads hash ids, org
   `org-agenda-files`, Dendron workspaces): the global layer is a pointer file that names where the
   projects are; sequence numbers and folder names do not travel across repos and machines.
10. **Nobody meters AI-agent spend per project with a burn-down** (c): OpenRouter caps a key,
    Helicone tags requests, Anthropic and OpenAI cap a workspace. A per-project agent budget with
    actuals is a gap, and Relay can attribute usage where a session is attached to a project.

Two warnings recur. Schema churn at the portfolio layer is expensive (a: Shortcut's Milestones to
Objectives migration, ClickUp's Space-or-Folder dilemma), so the project record should be
versioned and cross-cutting dimensions should be labels, not containers. And app-owned stores die
with the app (a: Height shut down 2025-09-24, Focalboard unmaintained, the Obsidian Projects
plugin discontinued May 2025; b: Logseq's DB beta data-loss warning): the truth stays Markdown in
git.

## 3. Proposal: Projects, a Relay feature above per-project Boards

A Relay user can track several projects from one place, including projects with no local repository
or Board. The global Projects view is the portfolio; each per-project Board remains the place for
that project's work. Cross-project work can live on the global Board and link to one or more
projects. A project is a first-class record with its own identity and metadata, not a copied Board.

Relay already has a per-user global Board at `$XDG_CONFIG_HOME/relay/switchboard` for memory and
alias records. It also has a local project registry (`state/projects.json`) and a Projects pane.
The product should join these surfaces instead of creating a second, unrelated tracker. One
possible implementation is a `project` Board card type in a `projects/` folder; the detailed type,
format, and migrations need an implementation pass because `board.CARD_TYPES` currently contains
only `work`, `memory`, and `alias`. The global Board location is a local default. Relay must not
silently create a remote repository or put a user's portfolio into a project repository.

### 3.1 Project identity and links

The minimum useful record is a stable ID, name, lifecycle status, priority, a short next action,
and links to zero or more local workspaces, Boards, or external URLs. A newly discovered workspace
can be offered as a project; an unlinked project can be created directly. One project can span
several roots, and an absent or offline root remains a valid link. Renaming a project or moving a
root must not break links from other Boards.

Optional metadata includes owner or collaborators, target date, health with a dated update,
category, custom stage, and planned human or agent budget. Domain-specific stages such as an
academic paper's submission pipeline belong to a template or custom field, not Relay's base
schema. Users who only need a list and priorities should see a small form.

The project record owns those portfolio fields. Open-card counts, active sessions, recent Board
activity, and model usage are derived from linked sources with provenance and a refresh time.
Relay must not claim it knows work done in external tools it cannot read. Cross-project work cards
can reference project IDs; one card may involve more than one project without being duplicated.

### 3.2 Priority and capacity

Project priority is separate from work-card priority. A short, ordered "Focus" group highlights
what matters now; Relay may suggest a limit, but should not make a fixed five-project cap an
invariant for teams or users with different portfolios. Changing project priority can influence
cross-project sorting without rewriting work cards. A user should also be able to filter by
status, category, owner, deadline, and health.

An optional budget has an amount, unit, period, and scope. Examples are planned hours per week for
a person or team and agent spend per month for a project. Show planned and actual values only when
Relay has a trustworthy actual source; otherwise show the plan alone. Pane focus time is a rough
activity signal, not billable time, and should be opt-in if offered at all. Usage accounting may
estimate model spend; display its coverage and uncertainty, especially for external agents,
subscription plans, and work outside Relay. The first release should report threshold crossings
without blocking a turn or changing models automatically.

### 3.3 The view and its review loop

The Projects view should offer a compact table and a board layout, with an obvious path from a
project to its local Board or workspace. The global Board can host the view; the existing Sessions
Projects pane should read the same project records. A global view of work cards is a query over
linked Boards, not a mirrored set of cards.

A dated project update may record health and a next action. Useful computed views are projects
without a next action, projects with stale updates, deadlines approaching, and budgets near or
above plan. These are prompts for review, not automatic diagnoses: no Board activity may simply
mean work happened elsewhere. A periodic review command or skill can offer changes for a user to
accept. It should work for every Relay user without requiring an outside service.

### 3.4 Storage, sync, and imports

Store portable project records under the user's Relay data directory by default, using the
Board's file conventions where practical. Keep machine-specific root locations separate from
portable identity and portfolio fields. The local project registry remains a discovery/cache
surface until migrations can be specified and tested. Export and backup must be possible; Git
sync may be offered, but is neither assumed nor required. A global Board may include private
records, so per-project sharing must not implicitly publish them.

Importers for Markdown folders, spreadsheets, and Trello can be optional later work. They should
preview mappings, preserve source links, support repeat runs without duplication, and never retire
or mutate the original tracker by default. The three examples in section 1 help test those
importers; none is a prerequisite to ship Projects. Two-way sync needs its own conflict and
privacy design before it is promised.

## 4. Product sequence and proof

| Stage | Deliverable | Proof |
|---|---|---|
| Foundation | Project records and stable links, create/edit/archive, discovery from Relay workspaces, projects without roots | Round-trip and migration tests; two projects sharing a root and one with no root stay distinct |
| View | Global Projects list, Board/session navigation, cross-project references and read-time Board rollups | UI check with several fixture Boards; offline and missing roots are shown accurately |
| Priority and review | Focus ordering, status and health updates, stale/stuck/deadline views | Sorting and report tests; a human can correct a false stale signal |
| Budgets | Optional time and agent budgets with attributed actuals and coverage labels | Usage fixtures reconcile to the displayed totals; missing usage is visibly missing |
| Imports | Previewed, repeatable import from common formats | Idempotence, privacy, and conflict tests on synthetic fixtures; real personal data is never a required test fixture |

The foundation and view should form one usable product slice. A record type with no visible
place to create, open, or review projects would repeat the maintenance failure seen in section 1.

## 5. Product risks

- **Duplicate truths:** keep portfolio fields on the project and compute Board rollups; make the
  source and refresh time visible.
- **False certainty:** local activity does not measure external work, and model cost estimates may
  miss calls. Show unknown rather than zero.
- **Private data:** local records and imported content must remain private by default. Sharing,
  remote sync, and diagnostics need explicit boundaries.
- **Portability:** project identity survives path changes and different machines. Missing roots
  should not invalidate the record.
- **Schema pressure:** lifecycle status is general; domain stages and labels are configurable.
- **Budget pressure:** report overages first. Blocking agent calls or changing models needs a
  separate product decision and clear user controls.

## 6. Decisions worth making for the Relay feature

1. **Surface:** use one Projects view reachable from the global Board and the existing Projects
   pane (recommended), or create a separate top-level pane?
2. **Relationship to the Board:** make project records a Board type, with cross-project work cards
   linking to them (recommended), or keep project records in a separate store with a bridge?
3. **Minimum first release:** ship create/edit/link/navigation, priority, and read-time Board
   rollups together (recommended). Should any of those move to a later release?
4. **Budgets:** start with optional planned hours and agent spend, showing actuals only with a
   traceable source (recommended). Is there another budget that belongs in the core feature?

These are product design questions. Importing or relocating any particular person's Trello board,
spreadsheet, or folders is a separate, optional task.
