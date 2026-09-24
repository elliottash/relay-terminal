# Pass A — How PM products model the layer *above* a single project board

*Research pass for the Relay global project board. All URLs were fetched and read on 2026-09-24 (a few Asana/Notion/Shortcut pages via archive.org snapshots of their official help centers; live URLs cited). No commits; this file only.*

Every tool below started with a per-project board (Relay's `.board/` equivalent) and later grew a second object — the "portfolio layer" — answering Elliott's questions: which of ~90 projects are most critical, how much time each gets, what is cross-project. The successful designs converge on **a thin wrapper object with its own status/health/progress, priority, and dates — never a second copy of the cards.**

---

## Linear — Initiatives above Projects

**(1) Object & rollup.** Linear's hierarchy is **Initiative → Project → Issues**. Initiatives ([docs](https://linear.app/docs/initiatives)) group projects across teams and carry their own lifecycle status, target date, and a *health* attribute; the initiative page shows all member projects with per-project status, progress, health, and target date. Projects ([docs](https://linear.app/docs/projects)) themselves auto-compute progress from completed/canceled issues but can be switched to manual progress. Project health is a small enum (on track / at risk / off track), separate from status — status is where it is, health is how it feels.

**(2) Priority & capacity.** Issue priority is the fixed integer scale Relay already uses: Urgent(0) / High(1) / Medium(2) / Low(3) / No priority(−1) ([issue properties](https://linear.app/docs/issue-properties)). Crucially, **projects got their own priority in 2024** — independent of any issue's priority — and can be micro-adjusted ([changelog](https://linear.app/changelog/2024-07-25-priority-for-projects-and-micro-adjust)), and initiatives sort by priority/progress/health/target date. There is **no capacity or time-budget feature** at the portfolio level; Linear deliberately refuses time estimates.

**(3) Link back.** Not a copy: the initiative *contains* project references; progress aggregates one-way upward, while editing the project edits the underlying issues. Initiative → project is a real parent edge in the data model, not a view filter.

**(4) Storage/export.** Everything is API-first: the GraphQL schema exposes `Project` and `Initiative` entities with status/progress/health fields you can sync out ([GraphQL docs](https://developers.linear.app/features/graphql/working-with-the-graphql-api)). No Markdown, no file storage.

**Pain points.** No portfolio-level capacity/time budgeting at all (a documented absence); priority semantics duplicated at issue/project levels can drift.

## Jira (Atlassian) — Plans with expandable hierarchy

**(1) Object & rollup.** Above boards/projects, Jira Premium's **Plans** "combine work items from boards, spaces, and filters to create an all-encompassing plan that spans multiple teams" ([What are plans](https://support.atlassian.com/jira-software-cloud/docs/what-is-advanced-roadmaps/)). Plans let admins **add extra hierarchy levels above epics — initiatives —** and roll progress/schedule up through the hierarchy. The plan is a *sandbox*: "your changes won't be saved to Jira work items until you're ready," then you push back ("Save to Jira").

**(2) Priority & capacity.** New **capacity plans** allocate work per person "in hours, days, or percentages" against a fixed 40-hour week, viewable **by People or by Work ("projects or high-level work types like epics")** so you can "reallocate capacity toward priority projects" ([plan individual capacity](https://support.atlassian.com/jira-software-cloud/docs/plan-individual-capacity-for-your-team/)). Documented limitation: fixed 40h week, custom working days not yet supported.

**(3) Link back.** Bidirectional but staged: plan reads live issues, edits land back in issues only on save. This sandbox-then-commit model is unusual and worth copying conceptually.

**(4) Storage/export.** Issues export CSV; plan state lives in Atlassian's cloud, exported via CSV or the REST API; hierarchy-level config is admin-managed JSON-ish config.

**Pain points.** The initiative hierarchy is a *project-type configuration*, not data — third-party tools (e.g., Visor) exist specifically to import AR hierarchies ("import should respect hierarchies from advanced roadmaps… goals / initiatives" — [HN](https://news.ycombinator.com/item?id=31104083)). Complexity and Premium gating are the constant complaints.

## Asana — Portfolios + Workload

**(1) Object & rollup.** **Portfolios** ([help](https://asana.com/guide/help/premium/portfolios)) are containers of projects; the portfolio view lists each project with **status (on track / at risk / off track / on hold), completion progress chart (milestones + tasks), custom fields, start/due dates, owner**. A project can live in multiple portfolios. Status updates written on the project propagate to every portfolio containing it.

**(2) Priority & capacity.** **Workload** ([help](https://asana.com/guide/help/premium/workload)) assigns each task *hours* or an abstract "effort" and shows per-person allocation vs. a configurable daily capacity, over/under coloring across the timeline — effectively a person-weeks/percentage budget per person, scrollable by project. Priority is a free custom field on projects in portfolios.

**(3) Link back.** Portfolio rows are live references to projects, not copies; the status *update* is a distinct dated object on the project that rolls up read-only.

**(4) Storage/export.** Portfolios are first-class API objects ([API](https://developers.asana.com/docs/portfolios)); CSV/JSON export via API.

**Pain points.** Portfolios and Workload are both paid-tier (Advanced+); teams report the status-update cadence decays without an enforcer — which is why Asana makes "add status update" a portfolio-visible checklist item.

## Notion — databases + relations/rollups (no portfolio object)

**(1) Object & rollup.** Notion has no native "portfolio." The pattern everyone builds: a **Projects database**, a **Tasks database**, related via a **relation** property; the Projects side gets a **rollup** over tasks — "perform calculations… count, percent checked, sum, show original" including "percent per group" ([relations & rollups](https://www.notion.com/help/relations-and-rollups)). Progress %, status, and dates on the project are then computed properties.

**(2) Priority/capacity.** Anything: priority is just a select property; a Portfolio view is a filtered/grouped database view. No capacity concept natively.

**(3) Link back.** Fully **bidirectional relation**: checking a task instantly changes the project's rollup percent. But it's all one flat item store — the "layer above" is a *view*, not an object.

**(4) Storage/export.** Everything is blocks in a proprietary store; per-page Markdown/CSV export, full export as Markdown+CSV zip; API exposes databases/properties.

**Pain points.** DIY: users reinvent status enums and progress formulas badly; rollup semantics ("a lookup field behaving like a rollup") are a known confusion point ([HN migration story](https://news.ycombinator.com/item?id=47862257)).

## GitHub Projects — org-level projects spanning repos

**(1) Object & rollup.** A **Project** is an org- or user-level item store that "can track issues and pull requests across multiple repositories" ([about projects](https://docs.github.com/en/issues/planning-and-tracking-with-projects/learning-about-projects/about-projects)). **Custom fields** (text, number, date, single-select, iterations) live on the project; the built-in `Status` single-select (Todo/In progress/Done) is the rollup primitive; roadmap/table layouts chart by date/status. There is no object above the project — a "portfolio" is just another project with one row per tracked repo/project.

**(2) Priority/capacity.** Priority = custom single-select or number field. Capacity via **iteration fields** (fixed-length sprints) and per-issue estimates; no person-hours model.

**(3) Link back.** Rows *are* references to issues/PRs in repos — the cleanest "read-only rollup with one writable overlay field" model: the project owns only its custom fields (status override, priority, iteration), everything else lives in the repo.

**(4) Storage/export.** **Any view exports as .tsv** ([exporting project data](https://docs.github.com/en/issues/planning-and-tracking-with-projects/managing-your-project/exporting-your-projects-data)); full access via GraphQL `ProjectsV2` API ([using the API](https://docs.github.com/en/issues/planning-and-tracking-with-projects/automating-your-project/using-the-api-to-manage-projects)).

**Pain points.** Export is per-view TSV only (no full-project dump), so reporting tools re-scrape; no object above projects, so org-level roadmaps get modeled as giant projects — the exact anti-pattern Elliott wants to avoid.

## Trello — Workspace Table/Calendar views, Butler, Enterprise reporting

**(1) Object & rollup.** No portfolio object. **Workspace Table view** aggregates "cards from boards across your Workspace" in one read-only table (Premium/Enterprise) ([workspace table view](https://support.atlassian.com/trello/docs/workspace-table-view/)); similarly Workspace Calendar view; Enterprise gets workspace reports. Rollup is *visual only* — no computed progress, status, or health above the board.

**(2) Priority/capacity.** None natively; people bolt on custom fields and Butler.

**(3) Link back.** The only true cross-board write mechanism is **Butler**, incl. **card mirroring** — a mirror card updates when the original changes ([mirroring cards](https://support.atlassian.com/trello/docs/mirroring-cards/)) — i.e., a synced shadow row per project, updated by rules.

**(4) Storage/export.** REST API per board/card, JSON export per board; no board collection export.

**Pain points.** The rollup views are paywalled and read-only; the community's standard workaround is Butler mirrors + a "master board," which drifts. Elliott's Trello "Work" board is exactly this failure mode: one flat board of cards standing in for projects.

## Height (defunct)

Height modeled work as tasks with rich **attributes** and cross-list views, with autonomous status features — but **shut down September 24, 2025** ([founder's notice](https://height.app/); [HN](https://news.ycombinator.com/item?id=43454034)). Lesson: portfolio state stored in closed SaaS dies with the vendor; git-native files don't.

## Plane (open source)

**(1) Object & rollup.** Plane's hierarchy is **Workspace → Projects → work items**, with per-project **Cycles** (time-boxed sprints) and **Modules** (feature/phase groupings) ([core concepts](https://docs.plane.so/introduction/core-concepts)). Modules give progress by issue states, but **there is no object above the project** — no initiative/portfolio; cross-project oversight is only via workspace-level filtered views.

**(2) Priority/capacity.** Urgent/High/Medium/Low/None on issues; cycle capacity by issue count/points per member; nothing project-portfolio-wide.

**(3) Link back.** Views are saved filters — read-only rollups, no separate item.

**(4) Storage/export.** Self-hosted Postgres, REST + OpenAPI spec, CSV export; the closest of the group to "your files, your rules," but still a server, not files in git.

**Pain points.** The missing portfolio layer is the #1 feature-gap ask; modules can't span projects.

## Focalboard / Mattermost Boards (open source, unmaintained)

Boards + cards with properties, groupable by anything; no portfolio object, no rollups. The standalone repo is **explicitly unmaintained** ([README warning](https://github.com/mattermost/focalboard)), and Mattermost Boards is a plugin on its own deprecation path ([repo](https://github.com/mattermost/mattermost-plugin-boards)). Relational store + REST API. Cautionary tale: even open-source, DB-backed boards die; Markdown-in-git survives.

## Basecamp — The Lineup, Hill Charts, Mission Control

**(1) Object & rollup.** Above projects: **Mission Control** (dashboard of all projects) and **The Lineup** — a drag-to-rank list of *what matters most this quarter* across all projects ([features](https://basecamp.com/features)). **Hill Charts** replace % progress with a hand-drawn position on "figuring things out → making it happen," updated by humans, not math.

**(2) Priority/capacity.** **No capacity features, deliberately**; priority *is* position in the Lineup. "Most project management systems are bloated" is their stated design philosophy ([how it works](https://basecamp.com/how-it-works)).

**(3) Link back.** Lineup rows link to projects; hill charts live inside projects and are surfaced on the dashboard. No two-way sync because there is nothing to sync.

**(4) Storage/export.** Proprietary; decent REST API; no Markdown.

**Pain points.** Teams wanting capacity/Gantt hate it — deliberately. But the Lineup ("ordered by importance, for now") is the closest existing product to a *chairman's* global board, and Hill Charts are the best-known alternative to fake percentages.

## ClickUp — Spaces/Folders/Lists hierarchy + Workload

**(1) Object & rollup.** A fixed container hierarchy: **Workspace → Space → Folder → List → Task (+subtasks)** ([Intro to the Hierarchy](https://help.clickup.com/hc/en-us/articles/13856392825367-Intro-to-the-Hierarchy)). Spaces "can be organized by departments, teams, **high-level initiatives**, or clients." There is no separate portfolio object — the layer above is *structural*, and cross-cutting views (Everything view, Dashboards) roll up statuses/progress by location.

**(2) Priority/capacity.** Per-task priority enum; **Workload view** budgets per-person capacity in points/hours per day with over-capacity coloring; Dashboards add portfolio-style widgets.

**(3) Link back.** Views aggregate the same items — read-only rollups; Dashboards read; the hierarchy itself is the "above" structure.

**(4) Storage/export.** REST API incl. Lists/Tasks with custom fields; CSV export per view; no file format.

**Pain points.** The rigid hierarchy forces a choice (is a client a Space or a Folder?) that breaks as the org changes — the eternal complaint, and a reason to prefer *tags/labels* over containers for cross-cutting dimensions.

## monday.com — Portfolio boards over project boards

**(1) Object & rollup.** monday's documented portfolio solution: a **"portfolio board" containing one item per project**, each linked to its own "project board"; the portfolio shows **health snapshot, high-level status summary, budget tracking, planned vs. spent effort, and a "battery widget"** (% complete) ([portfolio solution article](https://support.monday.com/hc/en-us/articles/13337066797202-The-monday-com-portfolio-solution)).

**(2) Priority/capacity.** Priority is a column; **effort columns + Workload widget** give per-person time allocation; battery/status roll up from linked boards.

**(3) Link back.** Built with **Connect Boards columns** (link items) + **Mirror columns** (show live status from the project board) — i.e., the portfolio row is a *separate item* that references and mirrors specific fields. Rollup widgets (battery, workload) compute from those links.

**(4) Storage/export.** GraphQL API over boards/items/columns; Excel/CSV export per board.

**Pain points.** Mirror/connect columns are the universal workaround and the universal complaint (limit of 20 portfolios per plan on Pro; sync is field-by-field manual configuration).

## Shortcut — Objectives above Epics

**(1) Object & rollup.** **Objectives** are the layer above epics/milestones: a strategic objective has **key results with target metrics and progress**, while *tactical* objectives contain epics whose story states roll up into progress blocks ([objectives overview](https://help.shortcut.com/help/objectives/objectives-overview)); Milestones were later replaced by Objectives ([migration note](https://help.shortcut.com/hc/en-us/articles/23307428661908)).

**(2) Priority/capacity.** Priority enum per story; no capacity budgeting; progress is derived from epic→story states.

**(3) Link back.** Objectives reference epics; rollup is computed, one-way; you edit the roadmap object, not the stories.

**(4) Storage/export.** Full REST API (stories, epics, milestones, objectives); CSV import/export.

**Pain points.** The Milestones→Objectives migration broke existing boards — schema churn at the portfolio layer is expensive for users (relevant warning: get the global board's schema right early).

## Airtable — Interfaces as the layer above bases

Airtable's rollup: one **base** = project(s); a **portfolio base** relates to project bases or shares tables; the layer above is the **Interface** — curated dashboards with permissions ("interface-only collaborators can view and interact with published interfaces without access to the underlying base" — Team/Business/Enterprise only) ([managing & sharing interfaces](https://support.airtable.com/articles/4255096925-managing-and-sharing-airtable-interfaces)). Rollup fields aggregate across relations; capacity is just numbers you invent. Pain: rollup/lookup semantics confusion (see the [HN migration thread](https://news.ycombinator.com/item?id=47862257)) and per-view (not per-base) data access for viewers — the "published snapshot" pattern is worth stealing though.

---

## Comparison table

| Product | Object above project | Rollup semantics | Priority / capacity / time budget | Link to per-project board | Storage / export |
|---|---|---|---|---|---|
| Linear | Initiative | Own status + health + auto/manual progress; target date | Integer priority 0–3 + none, on projects & initiatives too; no capacity | Parent edge, one-way progress | GraphQL API (`Project`, `Initiative`) |
| Jira (Plans) | Plan / Initiative hierarchy level | Sandbox plan; scheduled rollup, dependencies | Priority rank; **capacity plan: hours/days/% per person**, 40h baseline | Bidirectional, staged ("Save to Jira") | CSV, REST API |
| Asana | Portfolio | Status enum + progress chart from milestones/tasks; dated status updates | Custom-field priority; **Workload: hours/effort vs daily capacity** | Live reference + read-only status rollup | REST API (portfolios), CSV |
| Notion | (DIY) Projects DB + rollups | Computed % via rollup over related tasks | Any property; no capacity | Bidirectional relation | Markdown+CSV export, API |
| GitHub Projects | (none — org project spans repos) | Built-in Status single-select; custom fields; roadmap layout | Custom field priority; iterations | Rows are repo issue references; only overlay fields are project-owned | **Per-view .tsv export**; GraphQL API |
| Trello | (none) | Read-only Workspace Table/Calendar (paid) | Custom fields; Butler rules | Butler **card mirroring** (synced shadow) | Per-board JSON, REST API |
| Height | Cross-list views/attributes | Views only | Attributes | Views | Shut down 2025-09-24 |
| Plane | (none) | Cycle/module progress within project | Issue priority enum; cycle capacity | Saved filter views | Self-hosted Postgres, REST, CSV |
| Focalboard | (none) | Group-by only | Card properties | — | Relational DB, REST; unmaintained |
| Basecamp | Mission Control / **The Lineup** | Ordered "what matters now"; **Hill Charts** (human position, not %) | Priority = Lineup position; no capacity | Links only | REST API |
| ClickUp | Space/Folder/Lists as structure | Everything view, Dashboards | Priority enum; **Workload (points/hours/day)** | Read-only rollups | REST API, CSV per view |
| monday.com | Portfolio board | Battery %, health snapshot, budget, planned vs spent | Priority column; effort + Workload widget | **Connect + Mirror columns** (separate item, mirrored fields) | GraphQL API, Excel/CSV |
| Shortcut | Objective (strategic/tactical) | Key results / progress blocks over epics | Priority enum; no capacity | Computed references | REST API, CSV |
| Airtable | Interface (+ portfolio base) | Rollup fields over relations | Invented fields | Relations (bidirectional) | CSV per view, API |

---

## Patterns worth stealing for a file-based, git-native global board

1. **The layer above is a thin *wrapper* with its own status, not a copy of cards** (Linear Initiatives, Shortcut Objectives). A global project card holds: lifecycle status, health, priority, target date, and references — never the project's cards.
2. **Separate the three orthogonal signals** (Linear projects, Asana portfolios): *status* (planning/executing/…, matching the existing column set), *health* (on-track/at-risk/off-track — human-set, deliberately subjective), *progress %* (computed). Never collapse them into one field.
3. **Compute progress from the board, but allow manual override** (Linear project progress; Shortcut tactical objectives). For Relay: `progress: auto | manual`, auto = fraction of done cards, overridable in front matter.
4. **Dated, append-only project updates with a mandatory health pick** (Linear project updates, Asana status updates). Each update = a front-matter-stamped entry in the card's thread; the *latest* update is what the global board displays. This is exactly Elliott's "Last Meeting" column, upgraded.
5. **Portfolio priority is its own integer field, independent of card priority** (Linear's 2024 project priority). Reuse the −1..+3 scale, but scope it: card priority ≠ project criticality ≠ Lineup rank.
6. **An explicit ranked "what matters now" list of ~5–10 projects** (Basecamp The Lineup) — a single `lineup.md` with ordered slugs beats per-project priority sorting when everything is "priority 2."
7. **Global board = read-only rollup over the same files, one writable overlay** (GitHub Projects: rows reference repo issues; only custom fields are project-owned). The global `.board/` stores *links* (`repo: path`) plus overlay fields (criticality, time budget, delay-until), and never forks card content.
8. **If duplication is ever needed, mirror named fields, not the card** (Trello Butler card mirroring, monday Connect+Mirror columns). Define the sync contract narrowly: status, due, priority — with the per-project board always authoritative.
9. **Time budgets as hours-per-week per project, with over/under indicators** (Asana Workload, ClickUp Workload, Jira capacity plans' hours/days/%). Concretely: `budget: 6h/week` in the global card front matter, summed into a workload table — this replaces the Google Sheet's "active (1/0.5/0)" with real units.
10. **Plan vs. actual dates** (Linear target/actual completion, monday planned vs spent effort): `target_date`, `delay_until`, `last_contact` as explicit fields — direct transcription of the Sheet's "Delay Until" / "Last Meeting."
11. **A sandbox/staged-commit mode for portfolio edits** (Jira Plans: changes stay in the plan until saved): the global board is a git branch — propose re-planning, diff it, merge it. Git *is* the sandbox Jira had to build.
12. **Publish a generated, human-readable rollup artifact** (GitHub per-view .tsv export; Airtable "interface-only" published views; Notion page export): `make board` emits `BOARD.md`/`board.csv` from the card files — the portfolio layer's deliverable is a derived file, so the source of truth stays the per-project `.board/`.
13. **Schema churn at the portfolio layer is expensive** (Shortcut's Milestones→Objectives migration, ClickUp's Space-vs-Folder dilemma): version the global card schema in front matter (`schema: 1`) from day one, and prefer labels over new container levels for cross-cutting dimensions like "admin," "editing," "software."

*Sources: primary product documentation (Linear, Atlassian/Jira, Asana, Notion, GitHub, Trello, Plane, ClickUp, monday.com, Shortcut, Airtable, Basecamp, Focalboard, Height), fetched 2026-09-24; archived snapshots used where help centers are JS-rendered. Reddit/forum search was rate-limited, so complaint evidence is drawn from official docs' own limitations sections and Hacker News threads cited inline.*
