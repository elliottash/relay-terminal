# A global project board above the per-project Boards (research and proposed design, 2026-09-24)

Card #V3R3. The owner's ask: "i want to create a kind of global project board, where i can track
projects. i am thinking like trello […] do deep research on other systems, for a global project
management system that will then connect to all the project boards. so with that i can tag which
projects are most critical, maybe set time budgets per project. any thing cross project goes here."
He named three things he already has: a Trello board, a Google Sheet, and `~/project-notes`, "an aborted
effort, which i would like to actually make work here".

Three sourced research passes are kept whole under
[`research/global-project-board/`](research/global-project-board/): (a) [the portfolio layer in
fourteen products](research/global-project-board/a-products-portfolio-layer.md) (Linear, Jira,
Asana, Notion, GitHub Projects, Trello, Height, Plane, Focalboard, Basecamp, ClickUp, monday,
Shortcut, Airtable); (b) [plain-text, git-native and CLI
systems](research/global-project-board/b-plaintext-and-git-native.md) (Taskwarrior, org-mode,
Obsidian, Logseq, Beads, git-bug, git-issue, dstask, todo.txt, gh, Johnny.Decimal, PARA, Dendron,
Foam); (c) [prioritisation, time and agent budgets, and how academics track a paper
pipeline](research/global-project-board/c-priority-budgets-and-research-pipelines.md). A fourth
pass read the owner's three trackers through their APIs and files; its named data is private
(`relay-internal/planning/global-project-board-ground-truth.md`) and only its counts and shape
appear here. This page is the synthesis and the proposal. Nothing below is approved scope.

## 1. Ground truth: three trackers, one record, and why each one rotted

| Source | What it is | Size | What it holds per project | How it died |
|---|---|---|---|---|
| Trello "Work" | five lists by horizon: TODAY, soon, Deadlined, soonish, Fun | 38 open cards, 7 with a due date, 6 labels all unnamed and unused | nothing: 2 of 38 cards name a research project; the rest are single actions (a report, an email, a purchase) | it did not; it is a to-do list, and a good one. It was never a project board |
| Google Sheet "Projects Dashboard" | Working tab (29 rows) and On Hold tab (83 rows) | 112 rows; Working columns Co-Authors, Title, active (1 / 0.5 / 0), Stage (0 Planning … 9 2nd R&R), Journal | the real registry: stage, an activity weight, co-authors, target journal | the On Hold header also promises Who is working, Priority, My Turn, Last Meeting, Delay Until: Priority, My Turn and Delay Until are empty on every row, Last Meeting is filled on 8 rows and all of them are 2018–2021. Half the tab's rows no longer follow the header (the stage moved two columns to the right). The fields the owner is asking for now were designed once and never maintained |
| `~/project-notes/` | one folder per project with a README (Summary, Status, Collaborators, Search Terms, Links, To Do, Notes), an index README, a Warp skill, symlinked into Dropbox, no git | 88 folders; 86 have open to-dos; 29 have a dated note | the best-structured record: a summary, people with emails, links out, to-dos, dated notes | everything but two folders was written in one bulk session on 2026-03-08 and one folder was touched since. Nothing read the READMEs, so nothing kept them true |

The overlap: 77 of the sheet's 112 rows match an project-notes folder, 11 project-notes folders are in neither
sheet tab, and the union is roughly 120 research projects. Relay's own registry
(`state/projects.json`) knows ten projects on this machine, four with a board, and **none of them
is a paper**: the ~120 papers live in Dropbox, Overleaf and GitHub trees Relay has never been
pointed at, and the ten software and admin trees have no row in the sheet. Two disjoint universes,
which the global board has to be the one place that names both.

Every source ended up with the same fields — title, people, who is working now, a stage on a 0–9
pipeline, active or parked, target journal, whose turn, last touched, links out, a short to-do
list — and each died the same way: **nothing read the record, so nothing complained when it was
stale.** That is the design constraint. A global board that is only written by the owner will be
the fourth abandoned tracker; one that is *read* by Relay's agents and *computed from* the
per-project boards has a reason to stay true.

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
   "not the source of truth". Johnny.Decimal's index and project-notes's index README are the same failure.
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
    actuals is a gap, and Relay is the one product that already routes every model call through
    a pane that knows its project.

Two warnings recur. Schema churn at the portfolio layer is expensive (a: Shortcut's Milestones to
Objectives migration, ClickUp's Space-or-Folder dilemma), so the project record should be
versioned and cross-cutting dimensions should be labels, not containers. And app-owned stores die
with the app (a: Height shut down 2025-09-24, Focalboard unmaintained, the Obsidian Projects
plugin discontinued May 2025; b: Logseq's DB beta data-loss warning): the truth stays Markdown in
git.

## 3. Proposal: the global Board grows a `project` card type

Relay already has the container. The owner decided on 2026-09-17 (`TASKS-AND-MEMORY-DESIGN.md` §9,
decision 6) that a **global Board** at `~/.config/relay/switchboard/` "holds everything
cross-project" with "the same UI for both, switchable", and it exists today holding memory and
alias cards, read by the Globals pane and `globals_protocol.py`. Card #JN7X then made the
Switchboard strictly per-project, which is right for *cards*; it left the global Board with no
work in it. The global project board is that folder, with two additions:

- a fourth card type, `project` (`board.CARD_TYPES` is `work, memory, alias`), one file per project
  in `<global>/projects/<slug>.md`, and
- the global Board's `board.yaml` gaining a `projects` tab that renders that type as a table and a
  kanban by stage, and the ordinary work tabs (`todo`, `editing`, `ideas`, `deferred`, `done`) for
  the cross-project cards the Trello board holds today.

`~/project-notes` becomes `<global>/projects/` with a symlink left behind so the project-notes skill and the habit
keep working; its 88 READMEs are the seed. The folder is a git repository with a private remote
(the dstask pattern: one repository for the whole portfolio, synced with merge commits), which the
memory design already anticipated, and which replaces the Dropbox symlink whose failure mode (b:
org-roam refuses symlinked roots; Dropbox conflicts on concurrent edits) project-notes already had.

### 3.1 The project card

```markdown
---
id: EXPP                       # a card id like any other; the slug is the human name
type: project
schema: 1
slug: example-paper
kind: paper                    # paper | software | editing | grant | teaching | admin | fun
status: active                 # planning | active | semi-active | on-hold | submitted | published | archived
stage: 5-submit                # papers: 0-planning 1-analysis 2-slides 3-rough-draft 4-working-paper
                               #         5-submit 6-under-review 7-rr 8-rr-submitted 9-second-rr published
priority: 3                    # criticality, the existing −1…+3 flag; +3 is the lineup, capped at 5 cards
health: at-risk                # set by the latest update; on-track | at-risk | off-track | stalled
budget: {hours_per_week: 3, agent_usd_per_month: 40}
ball: {who: coauthor, since: '2026-09-10'}     # ball in court: who owes the next move
next: Send the draft to the coauthor
deadline: {date: '2026-10-15', whose: journal}   # mine | journal | grant | conference | coauthor
delay_until: null              # parked until; the sheet's Delay Until
people: [{name: A. Coauthor, email: coauthor@example.org, role: coauthor}, …]
journal: QJE
roots:                         # where the work is; the board(s) this card connects to
  - {path: ~/repos/example-paper, board: .board}
  - {path: ~/Dropbox/_Projects/example_paper}
links: {overleaf: …, drive: …, github: …, paper: …}
labels: [experiment]
created: '2026-03-08'
---
# Example Paper

## Summary
An example research project …

## Tasks
- [ ] Revise / present
- [ ] Share ExamplePaper code

## Notes
(dated entries; the thread holds updates)
```

Only the overlay is stored (pattern 1): status, stage, criticality, health, budget, ball, next,
deadline, people, roots, links. Everything about the *work* — open cards, what is executing, what
waits on the owner, the last commit — is **computed at read time** from the roots that exist on
this machine (pattern 2), shown in the row, and never written into the file. A root that is not on
this machine is shown as such, the way a remote host is; the card still carries the fields the
owner set. `touched` is likewise computed: the latest of the board's last thread entry, the root's
last commit, and the card's own last update.

The stage vocabulary is the sheet's own 0–9 pipeline, kept because it is the one the owner has
used for years and because every academic tracker in pass (c) converges on the same gates. `kind`
is what lets WIP limits and budgets be set per domain (c: the Kanban Guide) and lets editing,
grants and software sit on the same board as papers without a second board.

### 3.2 Criticality, the lineup and the cap

Criticality is the existing `priority` field (−1…+3, #VKFV), so the Board's filter language,
sorting and flag drawing need no new concept. Two rules make it mean something:

- **+3 is the lineup** (a: Basecamp), the ordered handful the owner is actually driving, and
  `relay-board.py check` warns above five. Demote before promoting (c: Burkeman's closed list).
- **A project's criticality weights its cards** (b: Taskwarrior): in any cross-project list — the
  Sessions pane's Projects tab, a "what should I do now" query from an agent — a card's rank is its
  own priority plus its project's. Nothing on the per-project board changes.

### 3.3 Budgets and actuals

`budget.hours_per_week` is the owner's own time (a: Asana Workload units; c: Silvia). The Projects
tab sums it against a weekly capacity set in the global `board.yaml` (`capacity: {hours_per_week:
40}`) and shows over-allocation, which is the honest replacement for `active = 0.5`.

`budget.agent_usd_per_month` is new (pattern 10). Relay is positioned to meter it without a proxy:
every pane is attached to a project (`RelayWindow::attachTab()` is the one funnel) and every model
call's cost is already accounted per session for the usage strip, so **actual agent spend per
project per month is a sum Relay can compute today**, and a burn bar beside the budget is a
rendering, not a metering project. Human hours are harder: the only passive signal is pane focus
time per attached project, which is a proxy and should be labelled one; the alternative is no human
actuals and a budget that is a plan, which pass (c) warns becomes decoration. This is a decision
for the owner (§6).

### 3.4 Updates, health and the reports that keep it true

A project update is a thread entry of kind `update` with a mandatory health pick and an optional
`ball` and `next` change (pattern 4). The row shows the latest one and its age. The Board's
existing agent console on a card is the place to write one: "Coauthor presented; the pilot
looks fine; my turn: regressions by Friday" becomes health, ball, next and a note in one turn.

Four computed reports, in the tab's header and available to agents as a `board_projects` query
(pattern 8):

| Report | Rule | Source |
|---|---|---|
| Stuck | active project with no `next` and no executing or planned card in any root | org-mode stuck projects |
| Stale | active project whose latest update or activity is older than 14 days and has no external deadline | flow metrics' work-item age; Gelman's periodic review |
| Ball overdue | `ball.since` older than 7 days when the ball is not the owner's | GTD Waiting For |
| Over budget | agent spend this month above 80 % of budget, or the hours sum above capacity | Toggl / Clockify alerts |

A weekly review is a skill (`/weekly-review`), not a UI: it walks the ten rules in pass (c) §(b)
over these reports and proposes moves — demote, park, kill, nudge — that the owner accepts per the
global board's autonomy setting, and records them as updates. That is the reader project-notes never had.

### 3.5 Connecting to the per-project boards, both ways

- **Down.** The project card's `roots` name the boards. Opening a project row opens its Board
  (`onOpenBoard` in `ProjectsPane`) or a session in its root; the card console can run a card
  there. The Sessions pane's Projects tab and the global board's Projects tab are one list: the
  registry (`state/projects.json`) becomes a per-machine cache of *which roots are here*, and the
  project card is the record. A registry entry with no card offers to create one.
- **Up.** A per-project `board.yaml` may carry `project: example-paper` so a moved root is found
  again, and so a pane's agent can answer "what is this project's criticality and budget" from its
  own board. The global card is authoritative; the local key is a pointer (pattern 9).
- **Sideways.** Cross-project work cards on the global board link to project cards with `#EXPP`
  like any card link, and a per-project card may link to a global one the same way; the Board's
  `#` picker searches both when the global board is open.

### 3.6 Imports, once

`relay-board.py projects import` with three readers, each idempotent through `import-state.json`
as the existing importers are (`PROJECT-INIT-AND-IMPORT.md`):

| From | To | Mapping |
|---|---|---|
| project-notes README | project card body and fields | Summary → `## Summary`; Status → `status`; Collaborators → `people`; Links → `links` and `roots`; To Do → `## Tasks`; Notes → thread entries dated as written; Search Terms → `labels` |
| Sheet Working / On Hold | project card fields | Title → match on slug and title (77 match today, 35 new cards); Stage → `stage`; active 1 / 0.5 / 0 → `status` active / semi-active / on-hold; Journal → `journal`; Co-Authors → `people` where project-notes has none; the eight Dropbox Paper links → `links.paper`; the On Hold column drift is handled by reading each row's stage from whichever column holds a `N - ` value |
| Trello Work | work cards on the global board | list → `todo` tab, with TODAY and Deadlined as `priority` +2 and +1 and `due`; description → body; the one card that names a research project links `#` to it; the Fun list → `fun` tab; one card title carrying a credential is imported with the secret stripped |

After the import the sheet and the Trello board are not deleted and not synced: the sheet may
receive a generated read-only export (`projects.csv`, a: GitHub's per-view TSV, Beads' JSONL: an
artifact, never a truth) if co-authors read it, and the Trello board is left as it is until the
owner stops opening it. Two-way sync with either is on the list of things every product in pass
(a) got wrong, and is not proposed.

### 3.7 What is not proposed

- **No second board UI.** The Board pane draws the global board with a `projects` tab; the row
  layout for a project is the one thing that is new (a table row: name, kind, stage, criticality,
  health and its age, budget bars, ball, next, open cards, touched).
- **No copying of cards upward.** A global "all executing cards" view is a read-time query over
  the roots on this machine.
- **No cost-of-delay scoring, RICE or WSJF fields.** Pass (c) rates them as churn at individual
  scale; criticality plus deadline-with-owner carries the same decision.
- **No per-project agent spend caps enforced by Relay** yet: the budget is shown and reported
  over, not refused. Refusing a model call mid-turn is a product decision with its own card.

## 4. Phasing

| Phase | What lands | Proof |
|---|---|---|
| 0. Record | `project` card type, schema 1, check rules (cap on +3, valid stage per kind, roots exist or are marked absent); `projects import` for project-notes, the sheet and Trello; `~/project-notes` symlinked to the global `projects/`; the global board as a git repository | `tests/test_board.py` round-trips a project card; an import run on the real sources yields ~120 project cards and ~38 work cards with the counts above, and a second run yields none |
| 1. Read | the global Board's `projects` tab in the Board pane: table and by-stage kanban, read-time rollup from local roots, the four reports, criticality weighting in cross-project lists, `board_projects` for agents; Sessions' Projects tab reads the same list | boardmodel tests for the rollup and the reports on fixture roots; a screenshot under `docs/qa_evidence/` with the owner's real board |
| 2. Budget | hours per week against capacity; agent spend per project per month from Relay's usage accounting; burn bars; over-budget report | a fixture month of usage events sums to the shown number; the owner's real month reconciles with the provider usage page within the accounting's stated error |
| 3. Review | the `update` thread kind with health; `/weekly-review` skill over the reports; the optional `projects.csv` export | a recorded review turn on the real board proposes at least one demote and one nudge that the owner recognises as right |

Phase 0 is small and fully specified by this page; phase 1 changes UI and is a design card of its
own; phases 2 and 3 each depend on a decision below.

## 5. Traps this design is built around

- **Two sources of truth.** The card holds overlay fields only; rollups are never written. The
  sheet and Trello are imported once. `projects.csv` is a generated artifact with a header line
  saying so.
- **The index that rots.** There is no index file; `BOARD.md` for the global board is generated as
  it is for every board.
- **Symlinked roots and Dropbox conflicts.** The global board is a git repository; roots are
  absolute paths per machine, and an absent root is a state, not an error.
- **Budget theatre.** Agent spend has real actuals from day one of phase 2; human hours are
  labelled as a plan unless the owner accepts a proxy.
- **Criticality inflation.** The check rule caps +3 at five; the review skill proposes demotions.
- **The infinite parked list.** `on-hold` projects with no update in a quarter appear in the
  review with two buttons: revive with a next action, or archive with a reason.
- **Schema churn.** `schema: 1` in every project card; new dimensions are labels.

## 6. Open questions for the owner

1. **Storage.** The global board at `~/.config/relay/switchboard/` becomes a git repository with a
   private remote and `~/project-notes` moves into it, symlink left behind (recommended); or project-notes stays a
   Dropbox folder and the global board points at it with `RELAY_GLOBAL_SWITCHBOARD`.
2. **The sheet.** Import once and stop editing it (recommended), with an optional generated
   `projects.csv` pushed to the sheet for co-authors; or keep the sheet as the editing surface and
   sync it in (not recommended: it is the drift every product in pass (a) suffers).
3. **Trello.** Import the 38 cards into the global board's `todo` tab and leave the Trello board
   as is (recommended); or keep Trello for actions and only take projects here.
4. **Criticality.** Reuse `priority` −1…+3 with +3 capped at five as the lineup (recommended); or a
   separate ordered `lineup.md` as Basecamp does.
5. **Budgets and actuals.** Hours per week and agent dollars per month (recommended units). Should
   Relay meter the owner's own hours by pane focus time per attached project, labelled a proxy, or
   leave human hours as a plan with no actuals?
6. **Agent budget enforcement.** Report over-budget only (recommended for phase 2); refusing or
   downgrading model calls for a project over budget is a separate card if wanted.
7. **Where it opens.** The Board pane with a global toggle and the Sessions pane's Projects tab
   showing the same list (recommended); or a new pane.
8. **Name.** "Projects" for the tab and the card type (recommended); the global board itself keeps
   whatever the Globals pane is called.
