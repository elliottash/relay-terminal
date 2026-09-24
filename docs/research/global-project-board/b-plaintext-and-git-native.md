# Pass B: How plain-text / git-native / CLI systems handle many projects and aggregate across them

*Research for Relay's global project board. All claims verified against primary documentation (fetched and checked 2026). Citations are inline.*

Elliott's situation — ~90 research projects scattered across git repos and Dropbox folders — is the exact scaling problem this class of tool hits. Each system below answers five questions differently: **where the registry of projects lives** (one file, one folder per project, or discovered by scanning), **how cross-project aggregation is computed** (scan at read time, committed index, or cache/db), **how priority and effort/budgets are represented**, **how a global item links to a per-project item**, and **what breaks across repos and machines**.

## 1. Taskwarrior + Timewarrior

**Registry:** none on disk — a taskwarrior data dir (`~/.task/*.data`) holds flat task records; projects are just a string attribute on each task ([man task(1)](https://taskwarrior.org/docs/man/task.1/)), hierarchical by dotted name (`project:Home.Kitchen`), so a project tree exists without any folder structure.

**Aggregation:** computed at read time from the flat store. `task projects` "shows all project names used", and `task <filter> summary` "shows a report of aggregated task status by project" ([reports](https://taskwarrior.org/docs/report/)). Because everything is one flat store, cross-project views are free — the trade-off is the registry *is* the store, so you cannot have a task live "in a repo".

**Priority/effort:** the centerpiece is the **urgency polynomial** — each task gets a score summed from weighted terms, with the coefficients configurable in `.taskrc`: `due` 12, `blocking` 8, `scheduled` 5, `active` 4, `age` (max 4), tags, and crucially `urgency.user.project.<name>.coefficient` and `urgency.user.tag.<name>.coefficient` (both defaulting to 1.0) for **per-project and per-tag boosts** ([urgency doc](https://taskwarrior.org/docs/urgency/)). This is the "flag the most critical projects" mechanism done declaratively: raise a coefficient, and everything in that project outranks others without touching the tasks. `next` ordering is just a filter on this score. Numeric effort exists as an optional UDA ([UDAs](https://taskwarrior.org/docs/udas/)); there is no native budget feature.

**Time budgets:** delegated to **Timewarrior**, which tracks tagged intervals; `timew summary` shows per-day intervals with per-tag subtotals and a grand total over any date range ([summary](https://timewarrior.net/docs/summary/), [tags](https://timewarrior.net/docs/tags/)), and a Taskwarrior hook auto-starts timers from tasks ([integration](https://timewarrior.net/docs/taskwarrior/)). A "budget" = your `timew summary` per `+project` tag compared against a number you keep elsewhere — the docs define no budget object.

**Linking:** a global item can reference a task only by UUID; there is no URL or path scheme for tasks. `context` subsetting ([context](https://taskwarrior.org/docs/context/)) is the nearest thing to a scope.

**Multi-machine/multi-repo:** one store per machine, synced by the taskchampion/taskd sync protocol ([task-sync(5)](https://taskwarrior.org/docs/man/task-sync.5/)) — designed precisely because syncing raw files over Dropbox produced corruption ([sync](https://taskwarrior.org/docs/sync/)). **Hooks** ([docs](https://taskwarrior.org/docs/hooks/)) (`on-add`, `on-modify`, `on-launch`, `on-exit` scripts with JSON stdin/stdout and exit-code semantics) let you mirror tasks into other stores — the natural place to push/pull from a per-repo board.

## 2. Emacs org-mode

**Registry:** `org-agenda-files` — an explicit list of files, or entries that are directories, in which case every matching `*.org` file in them is included ([Agenda Files](https://orgmode.org/manual/Agenda-Files.html)). A single variable is the whole "global board registry"; `org-agenda-text-search-extra-files` can hold files searched but not shown ([Agenda Files](https://orgmode.org/manual/Agenda-Files.html)).

**Aggregation:** recomputed on every agenda build by reading all registered files; no committed index, no cache that survives the session. Views span every file: global TODO list, weekly agenda, stuck projects. This is the cleanest "scan at read time" model — and its known cost: agenda speed degrades as the file list grows.

**Priority/effort/budgets:** rich. Per-file `#+TODO:` keyword sequences define workflows per project file ([TODO](https://orgmode.org/manual/TODO-Basics.html)); `[#A]`–`[#C]` cookie priorities; the `:EFFORT:` property feeds column view, e.g. `#+COLUMNS: %40ITEM(Task) %17Effort(Estimated Effort){:} %CLOCKSUM`, and the agenda offers a column view that sums estimated effort per day ([Effort Estimates](https://orgmode.org/manual/Effort-Estimates.html)). Actual time comes from `CLOCK:` entries, and the clock table can span `:scope agenda` — **all agenda files** — or an arbitrary file list or even a function returning file names ([Clocking Work Time](https://orgmode.org/manual/Clocking-Work-Time.html)). That is a native cross-project time-budget report: estimated vs clocked per project, per week. Org even has a cross-project *health* report: **stuck projects** — a custom agenda search for projects (TODO entries with subentries) that lack a next action, configured via `org-stuck-projects` ([Stuck Projects](https://orgmode.org/manual/Stuck-Projects.html)).

**Linking:** links are paths or `id:`/`org-id` URIs; a global item can link to `file:~/repos/paper1/board/x.md` or an org-id. Robust within Emacs, opaque to other tools.

**Multi-machine:** files synced via Dropbox/git; org's own answer to phone sync (MobileOrg) historically produced conflicts, and the manual's effort/clock totals silently depend on every machine having the same `org-agenda-files`. **org-roam** ([site](https://www.orgroam.com/)) shows the trap side: it uses one `org-roam-directory` with per-file IDs and explicitly warns that it "won't automatically resolve symbolic links" to the directory — Elliott's Dropbox-symlinked `~/project-notes/` pattern would break ID resolution.

## 3. Obsidian (Dataview, Bases, Kanban, Projects plugins)

**Registry:** discovered by scanning the vault — one folder of Markdown; "projects" are folders or tags, not entities.

**Aggregation:** all at read time, in memory. **Dataview** queries front matter: `TABLE status, priority FROM "Projects" AND #active`, with sources by folder, tag, or links ([queries](https://blacksmithgu.github.io/obsidian-dataview/queries/structure/)); it warns that vault-wide queries are expensive, so the "index" exists only per session. **Bases**, now a core plugin, is YAML view definitions in `.base` files — filters, sorts, groups, formulas, and a **Kanban view grouped by a property** — "all the data … is stored in your local Markdown files and their properties" ([intro](https://help.obsidian.md/bases), [views](https://help.obsidian.md/bases/views)). The `.base` file *is* the global board: a saved query, not a copy.

**Priority/effort:** whatever front-matter keys you invent (`priority`, `effort`, `budget`); Bases formulas can aggregate and compute, but there is no built-in urgency math or clocking.

**Linking:** wikilinks plus front-matter path references. The Kanban plugin's docs include a ["Link to vault"](https://publish.obsidian.md/kanban/Link+to+vault) page for boards kept in a different vault ([Kanban plugin](https://github.com/mgmeyers/obsidian-kanban)).

**Multi-machine:** the vault is a folder (Dropbox/iCloud/git); no merge tooling. The **Projects plugin** ([repo](https://github.com/marcusolsson/obsidian-projects)) — projects defined from folders *or Dataview queries*, with Table/Board/Calendar/Gallery views and per-project note templates — was **discontinued by its sole maintainer in May 2025** (notice in its README): a caution about building the global layer on one-person community plugins.

## 4. Logseq

**Logseq** One **graph** = one folder of Markdown/Org files (journals + pages) ([README](https://github.com/logseq/logseq)). Aggregation is by the app's in-memory DataScript DB: simple `{{query ...}}` blocks and advanced Datalog queries over blocks and properties ([Queries](https://github.com/logseq/docs/blob/master/pages/Queries.md)). Priorities/effort are block properties you define; tasks have `now`/`later`/`done` markers. Links are `[[page]]`/block IDs, internal to the graph. There is no multi-graph aggregation — each graph is an island, and the newer DB version ships its own SQLite store with a "data loss is possible" beta warning and an RTC sync product in alpha ([README](https://github.com/logseq/logseq)): the strongest demonstration that moving off plain files onto an app-owned DB makes multi-machine sync *harder*, not easier.

## 5. Beads (`bd`, 2025)

The most directly relevant recent design: a "distributed issue tracker for AI coding agents" ([README](https://github.com/gastownhall/beads)). **Registry:** every project repo gets a `.beads/` directory — one-folder-per-project by construction. **Storage:** an embedded Dolt (SQL) database in `.beads/embeddeddolt/` by default, or a server DB in `.beads/dolt/`; `.beads/issues.jsonl` is an **export for interchange and viewers, explicitly "not the source of truth or a backup"** ([README](https://github.com/gastownhall/beads)). **Aggregation:** queries run against the local DB (`bd list`, dependency graphs); cross-repo aggregation is not a query but a *routing* decision — `BEADS_DIR` and `--contributor` mode direct planning issues for a repo you can't commit to into a separate local database, e.g. `~/.beads-planning`. **IDs:** content/field-hash IDs like `bd-a1b2` — stable across repos and machines. **Priority:** `block`/`urgent`/`high`/`normal`/`low`; effort is story-point-ish, no clocking. **Sync across repos/machines:** `bd dolt push`/`bd dolt pull` against `refs/dolt/data` on an ordinary git remote ([sync-concepts](https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md)) — the remotes that carry the code also carry the issues, so a "hub repo" is just one more remote. This is the strongest published answer to "global layer above many repos."

## 6. git-bug, git-issue, dstask

**git-bug** ([README](https://github.com/MichaelMure/git-bug)): issues live *inside the code repo* as git objects — "fully embedded in git: you only need your git repository"; `git bug push/pull` sync via normal remotes; bridges to GitHub/GitLab. Registry = per-repo, no global view at all; IDs are internal object hashes. Shows the pure "git-native" end of the spectrum and its limit: aggregation across repos requires external scripting.

**git-issue** ([README](https://github.com/dspinellis/git-issue)): a hidden `.issues` directory in each repo, meant to be gitignored if coexisting with a project's own git. Each issue is a directory keyed by the **SHA of its initial commit**, with one plain file per field: `description`, `tags`, `milestone`, `duedate`, `weight` (positive integer), and `timeestimate`/`timespent` **in seconds** ([internals](https://github.com/dspinellis/git-issue)). Sync is `git issue push/pull` wrapping git on the issues repo. Note the schema: per-issue budget fields exist at the *file* level, not in a header — the same shape as Relay's front matter, and a precedent for effort/budget as first-class card fields.

**dstask** ([README](https://github.com/naggie/dstask), [DB format](https://github.com/naggie/dstask/blob/master/etc/DATABASE_FORMAT.md)): deliberately **one git repo for all tasks** (`~/.dstask`), UUID4 filenames, one directory per status (`active`, `resolved`, …), display IDs as 4-character unique prefixes for CLI ergonomics, and `dstask --pull` / `--push` that sync and auto-create merge commits. Priority is `P1`–`P5`-style tags plus project tags. It answers the multi-repo question in the opposite direction from git-bug: instead of pushing the tracker into every repo, it pulls all projects into one repo and links them by `+project` tag.

## 7. todo.txt

The minimal end: one line per task, `(A)` priority, `+project` and `@context` words, `x YYYY-MM-DD` completion, optional `due:`/`rec:` extensions ([format spec](https://github.com/todotxt/todo.txt/blob/master/README.md)). The CLI reads one `todo.txt` + `done.txt` per config dir; aggregating across many files means `cat *.txt | grep '+paper1'` yourself — the +project vocabulary is uncontrolled, so typos fork projects silently. No IDs, so linking is by quoting text. Its lesson: the cheapest possible registry, and the cheapest possible drift.

## 8. jira-cli and `gh` (hosted trackers, cross-repo views)

**jira-cli** ([README](https://github.com/ankitpokhrel/jira-cli)) is single-project-per-config: you switch projects by switching whole config files via `--config` or `JIRA_CONFIG_FILE` — no aggregation. **`gh`** does cross-repo aggregation properly, but *at the server*: `gh search issues --owner <owner>` searches every repo of an owner in one query (e.g. `gh search issues --owner github --archived=false`), and can filter on `--project <owner/number>` ([manual](https://cli.github.com/manual/gh_search_issues)). No local registry exists — the platform's index is the registry. That read-time-server-search model is what a Relay global board gets for free within one machine's repos, and cannot get across machines without a hub.

## 9. Folder-as-registry systems: Johnny.Decimal, PARA, project-notes-style

**Johnny.Decimal** ([intro](https://johnnydecimal.com/10-19-concepts/11-core/11.01-introduction/)) makes the folder tree *be* the registry: max 10 areas × 100 categories, unique `AC.NN` IDs on every folder, and "you create an index to link everything together." The registry is one file/folder-tree per machine; aggregation is human lookup. **PARA** ([Forte Labs](https://fortelabs.com/blog/para/)) is four top-level folders (Projects/Areas/Resources/Archives), one folder per active project, "organize by actionability": the global view is literally `ls Projects/`, and completion = moving the folder to Archives. Both are Elliott's `~/project-notes/` pattern done with discipline; both rot when the index isn't maintained or folders are renamed without updating IDs.

## 10. Dendron, Foam (workspace-above-notes systems)

**Dendron** ([repo](https://github.com/dendronhq/dendron)) is the closest structural match to a global layer over separate project stores: a **workspace** composes multiple **vaults** — "a git backed folder for your notes" — which you "mix and match … to separate concerns, like personal notes and work notes," and publish independently; its registry is its own `dendron.code-workspace` config. **Foam** ([repo](https://foambubble/foam)) scans one VSCode workspace (multi-root supported) at read time for wikilinks, backlinks, and orphans. Both demonstrate the "workspace = configured list of roots" registry: cheap to implement in Relay, and dependent on links staying valid.

## 11. Marginal cases

**jj (Jujutsu)** workspaces are multiple working copies of *one* repo sharing an op-log — about parallel work, not aggregating projects; not a registry pattern ([working-copy](https://jj-vcs.github.io/jj/latest/working-copy/)). The **nix flake registry** is, though: a global JSON file mapping short aliases to flake repo URLs, layered per-user/per-system — a pure "many repos, one alias table" registry ([NixOS/flake-registry](https://github.com/NixOS/flake-registry)). **klog** ([site](https://klog.jotaen.net/)) is plain-text time tracking where every entry carries `#tags`; totals per tag across many files give per-project budgets with `klog total` — the minimal Timewarrior.

## Comparison table

| System | Project registry | Cross-project aggregation | Priority / effort / budget | Global→project link | Cross-repo/machine |
|---|---|---|---|---|---|
| Taskwarrior | none (flat task store; `proj.sub` names) | read-time reports (`projects`, `summary`) | urgency polynomial, per-project coefficients; Timewarrior tag totals | task UUID only | taskd sync protocol; hooks to mirror out |
| org-mode | `org-agenda-files` list/dir | scan at read time on every agenda | `[#A]` cookies, `:EFFORT:` + `%CLOCKSUM`, clock table `:scope agenda`, stuck-projects | `file:` paths, `id:` links | file sync (Dropbox/git); symlink/ID caveats (org-roam) |
| Obsidian Dataview/Bases | scanned vault; tags/folders | read-time queries, session-only index | free-form front matter; Bases formulas; Kanban view by grouped property | wikilinks; Kanban plugin "Link to vault" | folder sync, no merge |
| Logseq | one graph = one folder | in-memory DataScript, `{{query}}`/Datalog | block properties | `[[page]]`/block IDs | DB version: SQLite + RTC (beta, data-loss warning) |
| Beads | `.beads/` per repo | local SQL DB; contributor routing to separate DB | block…low; effort field | hash IDs `bd-a1b2` resolvable anywhere | `bd dolt push/pull` on `refs/dolt/data` of any git remote |
| git-bug | per-repo, in `.git` | none (per-repo only) | labels, per-bug | object hashes, repo-scoped | `git bug push/pull` to remotes |
| git-issue | `.issues/` per repo | `git issue dump` JSON, external scripting | `weight`, `timeestimate`/`timespent` (sec), `duedate` files | issue SHA + repo path | `git issue push/pull` (git) |
| dstask | one repo for ALL tasks | it *is* the aggregate | P1–P5 tags, `+project` tags | 4-char display ID → UUID | `--pull/--push` with merge commits |
| todo.txt | the txt file(s) itself | manual `grep`/`cat` | `(A)`–`(Z)`; nothing for effort | none (quote text) | file sync; conflict-prone |
| gh / jira-cli | server-side index | `gh search issues --owner` (server query) | labels, GH priority field | `owner/repo#123` URL | platform-hosted |
| Johnny.Decimal / PARA | folder tree itself (+index file) | human lookup | none built in | `AC.NN` ID; folder path | Dropbox/git; renumbering rot |
| Dendron / Foam | workspace config listing vaults/roots | read-time scan, link graph | front matter | `dendron://` URIs / wikilinks | vaults are git repos |

## Patterns worth stealing for Relay's global board

1. **Registry = a config file listing roots, not a copy of cards.** org's `org-agenda-files` (list or directory-with-regexp) and Foam/Dendron workspaces both make the global layer a pointer file; Relay's `~/.config/relay/switchboard/` should hold "these repo paths are on the board", never duplicated cards.
2. **Compute aggregation at read time; keep any index in memory or clearly derived.** Dataview, Bases, Logseq, and org all re-scan rather than commit an index, which is why they don't drift; Dataview's own docs flag vault-wide query cost, so cache per session, never commit ([queries](https://blacksmithgu.github.io/obsidian-dataview/queries/structure/)).
3. **Per-project priority coefficients instead of manual global ordering** (Taskwarrior `urgency.user.project.<name>.coefficient`): "flag the most critical projects" becomes a coefficient in the global config, and ranking across all boards falls out ([urgency](https://taskwarrior.org/docs/urgency/)).
4. **A "stuck projects" health report** (org `org-stuck-projects`): a global view that lists projects whose boards have no next-action card — cheaper and more honest than manually flagging critical projects ([Stuck Projects](https://orgmode.org/manual/Stuck-Projects.html)).
5. **Effort estimate and clocked time as sibling fields, rolled up by period** (org `:EFFORT:` + `%CLOCKSUM`, clock table `:scope agenda`; git-issue's `timeestimate`/`timespent` in seconds): gives real time budgets per project with actual-vs-estimated, computable from card front matter ([Effort](https://orgmode.org/manual/Effort-Estimates.html), [Clocking](https://orgmode.org/manual/Clocking-Work-Time.html)).
6. **Content-hash IDs for cross-repo reference.** Beads' `bd-a1b2` and git-bug's object hashes survive repos, renames, and machines; sequence numbers (git-issue, dstask prefixes) only resolve locally ([Beads README](https://github.com/gastownhall/beads)).
7. **Sync over namespaced refs on existing git remotes** (Beads `refs/dolt/data`; git-bug push/pull): the global board's aggregation data can live on a small hub repo's git remote without any new server or Dropbox ([sync-concepts](https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md)).
8. **An export/interchange format beside the truth.** Beads keeps `.beads/issues.jsonl` for viewers and states plainly it is not the source of truth — Relay could emit a read-only JSON/MD rollup for scripts and co-authors without ever letting it become a second truth.
9. **Route items for repos you can't write to into a central DB** (Beads `--contributor` + `BEADS_DIR` → e.g. `~/.beads-planning`): exactly Elliott's case where co-authors own some repos — global items about foreign repos live in the hub, linked by repo path + ID.
10. **One repo for all tasks when repos are cheap** (dstask): alternatively to hub-over-repos, a single `~/.switchboard` git repo whose `+project` tags map to repos, synced with merge commits — simplest multi-machine story of anything surveyed.
11. **Workspace "contexts" that scope every report** (Taskwarrior `context` affects `projects`/`summary`): a global board command like `relay board --context editing` should filter the whole aggregation, not single views ([context](https://taskwarrior.org/docs/context/)).
12. **The saved-query-as-board file** (Obsidian Bases `.base`, Dataview `TABLE … FROM`): global views (e.g. "every card in any repo with `priority >= 2` and no activity in 14 days") stored as query files, so the board is definition, not data ([Bases](https://help.obsidian.md/bases)).

## Traps

- **Index drift.** Any committed/generated index (Johnny.Decimal's index, Dendron's workspace registries, a Relay-cached rollup) silently diverges from the cards. Every system that avoids rot does read-time computation; the ones that commit indexes require discipline that never survives 90 projects.
- **Two sources of truth.** Beads had to write into its docs that the JSONL export is "not the source of truth"; the Projects plugin reading Dataview-created data created two schemas for the same notes. A Relay global board that copies card data out of `.board/` files will rot within weeks.
- **Dead and unresolvable links.** Wikilink-based systems (Obsidian, Foam, Logseq) accumulate orphans on renames; org-roam explicitly does not resolve symlinked directories — a direct hazard for Elliott's Dropbox-symlinked `~/project-notes/` ([org-roam](https://www.orgroam.com/)).
- **File-sync conflicts.** todo.txt/org/Dropbox setups corrupt on concurrent edits; the tools that solved it built real merge (dstask's merge commits, Beads' Dolt push/pull, taskwarrior's sync protocol). Kanban card files with append-only threads merge cleanly; *index* files do not.
- **App-owned databases.** Logseq's DB version ships a data-loss warning in beta ([README](https://github.com/logseq/logseq)); Taskwarrior moved data formats under versioned migration. Relay should keep plain Markdown as truth and treat any DB as a rebuildable cache.
- **Plugin rot.** The Obsidian Projects plugin was discontinued by its sole maintainer in May 2025 ([README](https://github.com/marcusolsson/obsidian-projects)); Dendron's development slowed. A global board's core must not depend on single-maintainer plugins.
- **Uncontrolled vocabularies.** todo.txt `+project` typos fork projects; folder registries (PARA, JD, project-notes) rot when folders move without renumbering/relinking. A global registry needs one canonical project name, validated against the scanned repos.
- **Sequence IDs don't travel.** git-issue and dstask IDs are repo-local; cross-repo references need a repo prefix (`owner/repo#123`) or a content hash.
- **Scan cost at scale.** Dataview documents vault-wide query expense; org agendas slow with hundreds of files. Relay should scan lazily per root and cache within the session.
