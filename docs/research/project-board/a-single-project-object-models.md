# Pass A — How single-project tools model the objects *inside* one project

*Research pass for the Relay per-project Board (the layer inside one project: cards, skills, memories, artifacts, sessions, and the links among them). All URLs were
fetched and read on 2026-09-25 unless marked *unverified*. Read-only pass; this file only, no commits. The portfolio layer above projects is covered in
`docs/research/global-project-board/a-products-portfolio-layer.md` and is not repeated here.*

Each product below gives one project several **kinds** of object (tasks, documents, files, procedures) and some way to **link** kinds to each other. The recurring
lesson: the products people stay with make the link a first-class, bidirectional, *typed* edge that is rendered on both ends, and give the project a home page that
is a short computed digest, not another list.

---

## GitHub repository as a project manager

**Object kinds.** One repository holds at least nine kinds, each with its own tab and numbering scheme: Code (files at a ref), Issues, Pull requests, Discussions,
Wiki (a separate git repo), Releases (a tag plus notes plus binary assets — "deployable software iterations you can package and make available for a wider audience
to download and use", [about releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)), Actions runs and Checks, Packages, and
repo-linked Projects. Issues and PRs share one number space, which is what makes `#123` unambiguous. Since 2025 issues also carry an org-defined **issue type**
(defaults `task`, `bug`, `feature`; up to 25 per org; filterable in projects) — a kind field rather than a label ([issue
types](https://docs.github.com/en/issues/tracking-your-work-with-issues/configuring-issues/managing-issue-types-in-an-organization)); sub-issues, issue types and
advanced search went GA on 2025-04-09 ([changelog](https://github.blog/changelog/2025-04-09-evolving-github-issues-and-projects/)).

**Links: three strengths.** (a) *Autolinks* are text: `#26`, `GH-26`, `owner/repo#26`, a 7-char-shortened SHA, `owner/repo@SHA`, and admin-configured custom
autolinks to JIRA/Zendesk ([autolinked
references](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/autolinked-references-and-urls)). A mention writes a
**cross-referenced** event onto the *target's* timeline ("The issue or pull request was referenced from another issue or pull request", [issue event
types](https://docs.github.com/en/rest/using-the-rest-api/issue-event-types)) — so a one-way textual mention becomes a two-way rendered edge, computed from the text,
not stored by the author. Caveat: "Autolinked references are not created in wikis or files in a repository" — only in conversations. (b) *Typed closing links*:
`closes|fixes|resolves #n` in a PR body (only when the PR targets the default branch) creates a `connected` edge shown in both sidebars under "Development", and
"When you merge a linked pull request into the default branch of a repository, its linked issue is automatically closed"; manual linking is capped at ten issues per
PR and must be same-repo, while keyword linking may be cross-repo ([linking a PR to an
issue](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/linking-a-pull-request-to-an-issue)). (c) *Hierarchy*: **sub-issues** are a real
parent/child edge, up to 100 per parent and "up to eight levels of nested sub-issues", cross-repo allowed, with progress rolled up to the parent and to project views
([sub-issues](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/adding-sub-issues)). Blocked-by / blocking **dependencies** went GA on
2025-08-21: "You can link up to 50 issues for each relationship type", set from the sidebar's *Relationships* section, and "Blocked issues are marked with a
'Blocked' icon on your project boards or repository's Issues page" ([changelog](https://github.blog/changelog/2025-08-21-dependencies-on-issues/), [creating issue
dependencies](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/creating-issue-dependencies)). So by 2026 an issue carries four typed
edges — parent/sub-issue, blocked-by/blocking, connected PR, duplicate — plus the computed cross-reference edge.

**Permalinks to artifacts.** A code permalink (pick lines, "Copy permalink") pins a blob at a SHA and "will render as a code snippet only in the repository it
originated in… This does not work in Markdown files, only in comments" ([permanent
links](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-a-permanent-link-to-a-code-snippet)). So the artifact link
is *content-addressed* (ref + path + line range) and the rendering is a live embed.

**Documents vs tasks vs files.** Documents are Discussions ("conversations about the project's direction and future in an open-ended format", categories,
mark-as-answer, "Convert open-ended issues into discussions", [about
discussions](https://docs.github.com/en/discussions/collaborating-with-your-community-using-discussions/about-discussions)) and the Wiki; tasks are Issues;
files/artifacts are Code at a ref, Release assets, Packages and Actions artifacts. Kind conversion is explicit and one-way (issue → discussion leaves a
`converted_to_discussion` event).

**Reusable procedure (closest to a skill).** Two: **issue forms / templates** in `.github/ISSUE_TEMPLATE/*.yml|md` plus a `config.yml` chooser, and org-wide defaults
in the `.github` repo ([issue and PR
templates](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/about-issue-and-pull-request-templates)); and
**reusable workflows**, YAML files in `.github/workflows` whose `on` includes `workflow_call`, invoked as `{owner}/{repo}/.github/workflows/{filename}@{ref}` with
typed inputs, secrets and outputs ([reusing workflows](https://docs.github.com/en/actions/sharing-automations/reusing-workflows)). Both are *files in the repo,
versioned by ref* — the strongest precedent for skills-as-files.

**Project home.** The repo root: file tree at the default branch, the README surfaced from root, `docs/` or `.github` ([about
READMEs](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes)), and an About sidebar
with description, topics, latest Release, Packages, and language bar (sidebar contents *unverified* from docs; observed UI).

**Where truth lives.** Code, templates, workflows, wiki: git. Issues/PRs/Discussions/Projects/Releases metadata: GitHub's DB, reachable by REST/GraphQL only.

## Trello

**Object kinds.** A board has lists of cards; a card is the only rich object. Everything else hangs off it: description, comments/activity, **checklists** (Premium
"advanced checklists" put an assignee and due date on each item; an item can be converted into a card, and "The checklist item name becomes the card title… Due date
and assignee are added to the new card", [checklists](https://support.atlassian.com/trello/docs/adding-checklists-to-cards/)), **attachments** (10 MB free / 250 MB
paid; cloud links from Drive/Dropbox/Confluence/Jira "simply store links to those web pages"; "Trello does not allow attaching local files without them being
uploaded to Trello's servers", [attachments](https://support.atlassian.com/trello/docs/adding-attachments-to-cards/)), custom fields, labels, members, dates, and
Power-Up sections. There is no document kind: a "doc" is a card description or an attached external page.

**Links.** The card is the hub and every link is an *attachment* of some kind: "Trello card attachments create a link between both cards" — attaching card A to card
B shows on both ([attachments](https://support.atlassian.com/trello/docs/adding-attachments-to-cards/)), and a link is rendered as a card if you paste a card URL.
**Mirror cards** (paid) are the strongest form: paste a card URL into a new card's title and "When you open a Mirror card, you're viewing and editing the source
card"; a mirror "has a special label and border that reflect the board and list where the original card is located"; archiving the source does not archive mirrors,
and "using Power-Ups, Card button automations, and deleting the card, are not available without navigating to the original card's location"; on a free Workspace the
same action "will instead create a Link card" ([mirroring cards](https://support.atlassian.com/trello/docs/mirroring-cards/)). Links are untyped (just "attachment"),
bidirectional only for card↔card, and not computed from text.

**Reusable procedure.** Two: **card templates** and **board templates** (copies carry "card descriptions" but "We don't copy comments, card activity, card members or
archived cards"; checklists can be copied from any other card on the board, [copying](https://support.atlassian.com/trello/docs/copying-cards-lists-or-boards/)); and
**Automation** (ex-Butler): rules (trigger → actions), card buttons, board buttons, due-date and calendar commands, capped at 20 enabled card buttons and 20 board
buttons, with runs metered per Workspace — 250/month free, 1,000 Standard, unlimited Premium — where "Each time an automation triggers, it uses one automation run,
and however many operations are needed for that automation's actions" ([automation quotas](https://support.atlassian.com/trello/docs/butler-quotas-and-limits/)).
Automations are per-board or per-user, not files, and they are the closest thing to a skill: a named button that executes a checklist of actions on the card in front
of you. Power-Ups add card-back sections and badges from third-party services ([Power-Ups](https://support.atlassian.com/trello/docs/what-are-power-ups/)).

**Project home.** The board itself: lists of card fronts with badges (due, checklist n/m, attachment count, members, labels). No digest, no description page; the
"About this board" is a menu item.

**Where truth lives.** Trello's DB only; per-board JSON export via the API. Attachments are copies on Trello's servers or bare URLs.

## Notion

**Object kinds.** There is exactly one primitive, the page, and one container, the database; a "kind" is a database. The official **Projects & Tasks** template is
two databases with a relation: "Projects are the parent of tasks", "One project can have many tasks, but each task should only relate to a single project", and "The
completion property calculates the number of completed tasks per project and displays the percentage as a progress bar" ([getting started with projects and
tasks](https://www.notion.com/help/guides/getting-started-with-projects-and-tasks)); the "Projects, tasks & sprints" variant adds a third Sprints database related to
tasks. Documents are pages (in a wiki database or free-floating); files are attachments or file properties; there is no artifact kind.

**Links.** Everything is a **relation** property. "By default, relations are one-way", and toggling "Show on [related database]" makes it two-way: "With two-way
relations, the edits work both ways!"; a self-relation "can work in both directions… If you add Task B to Task A, Task A can also show up on Task B"; a relation can
be limited to "1 page" or "No limit"; **rollups** compute over the relation (show original, count, percent not empty, sum/avg/min/max, earliest/latest date) and
"Rollups can only be sorted when they output a numeric value" ([relations and rollups](https://www.notion.com/help/relations-and-rollups)). Sub-items and
dependencies are sugar over self-relations: "Sub-items allow you to break tasks into smaller, distinct pieces of work"; "Adding a dependency lets you connect tasks
to each other in a linear way", with date-shifting policies "Shift only when dates overlap", "Shift & maintain time between items", "Do not automatically shift" and
an option to "prevent shifted items from starting or ending on weekends" ([sub-items and dependencies](https://www.notion.com/help/tasks-and-dependencies)). Typed in
the sense that each relation *property* has a name; only "Blocked by/Blocking" and "Parent item/Sub-item" carry built-in semantics. Inline `@page` mentions are not
relations and produce no backlink property (they do appear in the page's Backlinks list — *unverified* on this pass).

**Knowledge with an expiry.** A page turned into a **wiki** ("Select ••• … Turn into wiki") gets an Owner and a Verification property: "By default, whoever creates a
page in a wiki becomes the page's owner", you can "verify the page until a specific time or indefinitely", and when it lapses "page owners will be notified to
re-verify the page"; the wiki "Home" view "shows you all of the pages within the wiki"; Business/Enterprise only ([wikis and verified
pages](https://www.notion.com/help/wikis-and-verified-pages)). This is the one mainstream model of *memory with a freshness contract*.

**Reusable procedure.** **Database templates** prefill properties and body ("a template for bug reports that automatically puts P1 in the Priority property"), may
"contain any type of content, including images, embeds, and sub-pages", can repeat "daily, weekly, monthly, or yearly", with "only three levels of nesting per
database template", and the docs warn not to prefill a relation "unless you want every page you create with that template to relate to the same existing page(s)"
([database templates](https://www.notion.com/help/database-templates)). **Buttons** are inline procedures: "Insert blocks", "Add page to… and edit the properties of
that page", "Edit pages in", "Show confirmation", "Open page or URL", plus notifications, webhooks and variables ([buttons](https://www.notion.com/help/buttons)).
Neither is a file; both live in the workspace DB.

**Project home.** Whatever the template's project page holds: properties at top, then a linked view of its tasks filtered to the relation, and free-form notes.
Nothing is computed beyond rollups.

**Where truth lives.** Notion's block store; Markdown+CSV export loses relations to plain text.

## Linear

**Object kinds.** Team → Issues (with sub-issues), Projects (cross-team), Milestones inside a project, Cycles (team time-boxes), Labels, Project **documents** and
external **resources**, Project **updates**, Initiatives above. A project's page has an Overview tab with "both a brief summary and an extensive description" (the
description edits "similarly to a document"), properties (status, lead, teams, labels, start/target dates, members), a Resources section to "link to external
resources" and "create documents inside of Linear to track project specs, updates, and notes", the milestone list, and a graph that "shows scope, velocity, progress
over time, and generates live predictions for when the project will complete" ([project overview](https://linear.app/docs/project-overview)).

**Links: typed, bidirectional, some computed.** Issue relations are *blocks / blocked by*, *related*, *duplicate*, plus parent/sub-issue. "When you reference issues
in a description or comment, they'll automatically become a related issue" — a computed edge from text, like GitHub's cross-reference; a blocking edge shows on both
issues with colour-coded flags, and "Once the blocking issue has been resolved, the relationship moves under Related" — the edge *decays* on completion; "Duplicate
issues show a link to the original issue directly in the issue view, including a dedicated banner"; no cap: "add as many related, blocking, blocked, or duplicate
relations as you like" ([issue relations](https://linear.app/docs/issue-relations)). Documents are first-class link targets: "reference documents in issue
descriptions, comments, agent guidance fields and even in other documents by typing @", and "Every document (and project description) has version history" ([project
documents](https://linear.app/docs/project-documents); page is JS-rendered, quotes via its search snippet). Git is linked by convention: a PR links to an issue when
"the issue ID appears in the branch name, the issue ID appears in the PR title, or a magic word plus the issue ID appears in the PR description" (magic words
`close/fix/resolve/complete/implement` and their inflections; "Refs ENG-88" links without closing), and the integration "creates a link to the PR in the Linear issue
and a linkback to Linear from the PR" ([GitHub integration](https://linear.app/docs/github-integration); JS-rendered, quotes via search snippet).

**Status as a dated object.** A project update "consist[s] of a health indicator… and a rich text description"; health is exactly "On track, At risk, or Off track";
"The most recent update is displayed on the Project Overview"; "Updates appear in chronological order along with any changes to properties such as the target date,
members, and milestones"; admins "can enable update reminders" ([initiative and project updates](https://linear.app/docs/initiative-and-project-updates)). The update
stream is the project's memory of *how it felt when*.

**Reusable procedure.** **Templates** at workspace or team level: standard (prefilled body) or *form* templates with typed fields; they can preset "team, status,
priority, assignee, delegated agent, project, labels, estimate, and sub-issues"; "Default templates are templates that are automatically applied when creating a new
issue, given the conditions are met" ([templates](https://linear.app/docs/issue-templates)). Note "delegated agent" as a presettable property, and the "agent
guidance" field on documents: Linear's 2025–26 model treats an agent as an assignee and a document as its instructions.

**Where truth lives.** Linear's DB via GraphQL; no files.

## Basecamp

**Object kinds — one per tool, all peers.** "Your new project starts completely empty. No tools are added by default — you choose exactly what you need": Message
Board, To-dos, Chat (Campfire), Card Tables, Schedule, Docs & Files, Doors, Email Forwards, Automatic Check-ins; "Tools appear in the order you add them, and you can
rearrange them anytime" ([creating and setting up a project](https://5.basecamp-help.com/article/1176-creating-and-setting-up-a-project)). Tasks are To-dos (lists of
items) and Card Table cards (a Kanban with Triage / Not now / columns / Done — [card tables](https://5.basecamp-help.com/article/1063-card-tables)); knowledge is
Message Board posts (long-form, "shouldn't disappear in a chat stream") and Docs; files are Docs & Files with version history ([docs and
files](https://5.basecamp-help.com/article/1079-docs-and-files)). Basecamp is the clearest case of *kinds as separate tools with separate pages*, no shared item
table.

**Links.** Weak and untyped: any recording can be @-mentioned or pasted as a URL into another; files uploaded to a message or to-do also appear under Docs & Files
(*unverified* on this pass). No relation model, no rollups, no dependency edge. The project home ("dock") is a grid of the enabled tools plus a **latest-activity**
feed and the schedule/assignments strip; it is a launcher, not a status page.

**Reusable procedure.** **Project templates** and **to-do list templates**: "Project templates can include tools, messages, to-dos, cards, files, and other project
structure"; "To-do list templates reuse lists, notes, files, and assignments across projects"; saving an existing project as a template resets it — "Completed to-dos
reset to incomplete, cards return to Triage, and chat history is removed"; dates are relative — "to-do due dates, card dates, and schedule events adjust based on the
project's start date" ([project and list templates](https://5.basecamp-help.com/article/1094-project-and-list-templates)). Automatic Check-ins (a scheduled question)
are the only recurring automation.

**Where truth lives.** Basecamp's DB; REST API (`bc-api`) per recording type.

## Jira + Confluence

**Object kinds.** Jira: work items (issue types Epic/Story/Task/Bug/Sub-task, admin-extensible), boards, sprints, versions, components, attachments, web links.
Confluence: pages, live docs, whiteboards, databases, attachments, in *spaces*. Tasks and knowledge are two products with two permission models, joined by links.

**Links: typed with inward/outward names, admin-defined.** Built-in link types read differently from each end: "is blocked by / blocks", "is cloned by / clones", "is
duplicated by / duplicates", "is implemented by / implements", "is caused by / causes", "relates to", and "Work item link types can be configured by Jira admins";
"Linked work items appear in the detail view by default" and optionally on board cards ([link work
items](https://support.atlassian.com/jira-software-cloud/docs/link-issues/)). This is the canonical *typed, directional, bidirectionally rendered* edge, and the
admin-defined vocabulary is the point: the type list is data, not code.

**Docs ↔ tasks.** A parent-level work item links pages: "Select + to add or create related work. Select Page, then Link page", with "up to 2,000 total links to a
work item, which includes Confluence pages along with web links" ([link a Confluence page to an
epic](https://support.atlassian.com/jira-software-cloud/docs/link-a-confluence-page-to-an-epic/)). From the other side, "The simplest way to add a Jira work item to
Confluence is to paste a Jira URL on a Confluence page", the Jira work items macro embeds a live query, "the Jira Links button appears at the top of the Confluence
page", and highlighting text lets you "Create single work item" or "create stories in Jira, and automatically link them to your epic" ([use Jira and Confluence
together](https://support.atlassian.com/confluence-cloud/docs/use-jira-and-confluence-together/)). So a pasted URL is promoted to a stored, two-way remote link — the
same move GitHub makes with cross-references, across a product boundary.

**Reusable procedure.** Confluence **templates** with variables ("they'll fill out a form with text fields or lists for each variable", space-admin-owned, plus
global templates and blueprints — [create a template](https://support.atlassian.com/confluence-cloud/docs/create-a-template/)); Jira **Automation** rules (trigger →
conditions → actions, project- or global-scoped, with a manual trigger; page fetch 404ed on this pass, *unverified* details); Jira issue-type schemes.

**Project home.** Jira: the board or backlog, with a "Summary" page (Cloud, 2024+) of counts and recent activity (*unverified*). Confluence: the space overview page,
a hand-edited page.

**Where truth lives.** Two cloud DBs; REST APIs; Confluence exports to HTML/PDF/XML, Jira to CSV/JSON.

## Fibery

**Object kinds are user-defined.** A workspace has *databases* (formerly "types") the user invents — Feature, Bug, Interview, Insight, Sprint — each with fields, and
a rich-text document body on every entity. Relation cardinality is "one-to-many, one-to-one, or many-to-many", and "A self-relation is simply a relation that has
been configured to link a Database to itself and behaves just like other Relations" ([self-relations
guide](https://the.fibery.io/@public/User_Guide/Guide/Self-relations-328)); the DB-design guide: "This is called setting the relation cardinality. It can be
one-to-many…, one-to-one…, or many-to-many", "A relation is visible from both ends", and deleting it on one side deletes it on the other; a lookup is "a sneak peak"
through an intermediate database ([how to design a database in Fibery](https://fibery.com/blog/guides/how-to-design-a-database-in-fibery/)). Every relation is a
*field on both databases* — typed by name, bidirectional by construction — and lookups and formulas compute over it. Documents are entities too (a Doc database or a
rich-text field), so "insight → feature" is the same kind of edge as "bug → sprint". Highlights (text in a doc linked to an entity) make a passage of knowledge a
link source.

**Reusable procedure.** Automations (rules and buttons per database, with a JavaScript action) and entity templates; the schema itself is the playbook.

**Project home.** No fixed home: a Space is a set of databases and saved views; the "home" is whichever view you pin.

**Where truth lives.** Fibery's DB; GraphQL/REST; Markdown export per document. (The user-guide pages are JS-rendered and returned no text on this pass; the
self-relation quote is from that page's search snippet, the rest from the blog guide.)

## Airtable

**Object kinds.** Tables in a base; a record is the only primitive; attachments are a field type. One-to-many vs many-to-many is a modelling choice: "Each work of
art can only be in one museum at a time, but each museum can have many works of art" vs "each author may have written multiple books" ([linked record
relationships](https://support.airtable.com/docs/understanding-linked-record-relationships-in-airtable)).

**Links.** A **linked record** field always creates its reciprocal in the other table (and "When self-linking, two fields will be created. One field is a 'To' field
and another is a 'From' field"); "Allow linking to multiple records" only restricts the picker — "Multiple links can still be created through automations or
copy/paste"; lookups and rollups are computed fields over the link. So: typed by field name, bidirectional by construction, with computed aggregates, and *no*
text-derived links at all.

**Reusable procedure.** Automations (trigger → actions, incl. run-a-script) and Interfaces; no template object below the base level.

**Where truth lives.** Airtable's DB; CSV export per view loses links to names.

## Plane and Height

**Plane (open source, 2026).** Workspace → Project → Work items, "organize[d] using Cycles (time-boxed periods)" and Modules ("Some work does not fit in a single
Cycle… grouped into a Module"), Views, project **Pages** ("an AI-powered notepad") and a workspace **Wiki** ([core
concepts](https://docs.plane.so/introduction/core-concepts)). Relations are typed with inward/outward names, three built in, custom ones definable: "The inward name
appears on the work item you're adding the relation to, and the outward name appears on the work item you're linking" and "The relation appears on both work items"
([custom relations](https://docs.plane.so/work-items/custom-relations)). Pages link to work: "Link directly to work items within your pages using the @ mention
feature", plus "work item embeds" among 16 slash-command blocks ([pages](https://docs.plane.so/core-concepts/pages/overview)); since 2025-09-01 pages nest ("Nest
pages inside a project instead of keeping everything in one long document"), work items recur from a template that "preserves context like assignee, priority, and
labels", and Notion/Confluence spaces import ([changelog](https://plane.so/changelog/2025-09-01-recurring-items-doc-imports-nested-pages)). A community MCP server
notes the trap: "Plane stores relations per item and has no view of the shape they make" ([issue](https://github.com/MrSampson/plane-mcp-server/issues/25)). Truth:
Plane's Postgres, self-hostable; API.

**Height (shut down 2025-09-24).** Tasks with chat threads inside, "combining tasks, chat, adaptive workflows"; Height 2.0 (Oct 2024) sold itself as "the autonomous
project management tool" with AI triage, backlog pruning and auto-updating specs; the shutdown note gave no reason
([Creativerly](https://www.creativerly.com/height-app-is-shutting-down/),
[AlternativeTo](https://alternativeto.net/news/2025/3/height-project-management-tool-to-shut-down-by-september-2025/)). The lesson for Relay is about custody: an
all-in-one-DB tool with agents writing the specs left users with an export and a deadline.

---

## Comparison table

| Product | Object kinds inside a project | How links are modelled | What the project home shows | Closest thing to a skill / playbook | Where truth lives |
|---|---|---|---|---|---|
| **GitHub repo** | Code@ref, Issues (typed, sub-issues), PRs, Discussions, Wiki, Releases+assets, Actions runs, Packages, Projects | Typed (parent/child, blocked-by, closes, duplicate) and bidirectionally rendered; `#n` mentions **computed** from text into timeline events; permalinks pin blob@sha | README + file tree + About sidebar (latest release, packages) | Issue forms in `.github/ISSUE_TEMPLATE`; reusable workflows `@ref` | Git for code/templates/workflows/wiki; GitHub DB for the rest |
| **Trello** | Card only, with checklists, attachments, custom fields, Power-Up sections | Untyped "attachment"; card↔card shows on both; mirror cards are live views; nothing computed | The board (lists of card fronts with badges) | Card/board templates; Automation rules and buttons (metered runs) | Trello DB; attachments copied to Trello |
| **Notion** | Pages in databases; kinds = databases (Projects, Tasks, Sprints, Wiki) | Relation properties, one-way by default, two-way on request; rollups computed; sub-items/dependencies are named self-relations | Project page: properties + filtered task view + notes | Database templates (repeating), Buttons, database automations | Notion block store |
| **Linear** | Issues, sub-issues, Projects, Milestones, Cycles, Labels, Documents, Resources, Updates | Typed (blocks, related, duplicate, parent) bidirectional; text mentions computed into *related*; blocking decays to related on resolve; PR links by branch/title/magic word with linkback | Overview: summary, description, properties, resources/docs, milestones, latest update, progress graph with prediction | Issue/form templates (can preset sub-issues and delegated agent); document "agent guidance" | Linear DB |
| **Basecamp** | One kind per tool: Message Board, To-dos, Card Table, Docs & Files, Schedule, Campfire, Check-ins | @-mention / URL only; untyped, not bidirectional, nothing computed | Dock of tools + latest activity feed | Project templates and to-do list templates with relative dates | Basecamp DB |
| **Jira + Confluence** | Work items (typed), sprints, versions; pages, live docs in spaces | Admin-defined typed links with inward/outward names, shown on both; page↔item remote links from pasted URLs | Board/backlog (Jira); space overview page (Confluence) | Confluence templates with variables; Jira Automation rules | Two cloud DBs |
| **Fibery** | User-defined databases, each entity with a doc body | Relation fields on both databases, cardinality chosen, deletions propagate; lookups/formulas computed | A pinned view; no fixed home | Automations per database; the schema | Fibery DB |
| **Airtable** | Records in tables; attachments a field | Linked-record field with automatic reciprocal; lookups/rollups computed; no text links | A view | Automations with scripts; Interfaces | Airtable DB |
| **Plane** | Work items, Cycles, Modules, Views, Pages (nested), Wiki, Intake | Typed inward/outward, three built in plus custom, shown on both; pages @-mention and embed items | Project overview/board | Recurring work items from a template; page templates | Postgres, self-hosted |
| **Height** (defunct) | Tasks with chat; AI-maintained specs | Sub-tasks, mentions | Task list | "Autonomous" AI rules | Height DB (exported at shutdown) |

## Patterns worth stealing for a file-based, git-native project board

1. **Promote mentions to edges, on the target.** GitHub turns `#123` in any comment into a `cross-referenced` event on issue 123's timeline; Linear turns a mention
   into a *Related* relation; Jira/Confluence turn a pasted URL into a stored remote link with a "Jira Links" button on the page. For the Board: a card, skill,
   memory or artifact that names another (`#ID`, path, `skills/x`) should get a computed **backlinks** block on the *named* object, derived at index time from the
   text, never hand-maintained.
2. **A small closed vocabulary of typed, directional edges, rendered from both ends with different labels.** Jira's inward/outward names ("blocks" / "is blocked
   by"), Plane's custom relation types, Linear's four types, GitHub's parent/sub-issue + blocked-by + closes. Store the edge once with a type, print it twice. Keep
   the type list as data (Jira, Plane) so "produced by run", "uses skill", "supersedes memory" can be added without code.
3. **Let edges decay or resolve.** Linear moves *blocks* to *related* when the blocker closes; GitHub closes the issue when the `closes` PR merges. Edges carry
   lifecycle, not just existence.
4. **Kinds are files-in-the-repo when they are procedures.** GitHub's issue forms (`.github/ISSUE_TEMPLATE/*.yml`), reusable workflows
   (`.github/workflows/*.yml@ref`), PR templates: versioned, reviewable, referenced by ref. This is exactly the skill model (`SKILL.md` folders) — commit skills next
   to cards, reference them by path, pin them by commit when a run cites them.
5. **Content-addressed artifact links.** GitHub permalinks pin `path@sha#L10-L20` and render as a live snippet inside the same repo. An artifact link in a card
   should be `path@commit`, and the Board should render a preview when the path exists at that commit.
6. **Progress is a rollup over a typed child edge, never a stored number.** Notion's completion property, Linear's project graph, GitHub sub-issue progress. A card's
   "done n/m" is computed from its sub-cards; a project's health is computed from its cards.
7. **Knowledge with a freshness contract.** Notion wiki verification: owner + expiry + re-verify notification. Memory cards should carry `owner`, `verified_until`,
   and the Board should list expired memories on its home page.
8. **Status is a dated stream, not a field.** Linear project updates: health enum + prose, shown newest-first on the overview, interleaved with property changes
   since the last update. Relay's `cases.jsonl` is already this shape; surface the latest entry on the project home.
9. **The hub object is the card, and links are attachments of any kind.** Trello's card back (checklists, attachments, card↔card links, Power-Up sections) and
   Linear's issue sidebar. A card's page should list its skills used, artifacts produced, memories cited and sessions that touched it as sections, each a typed edge.
10. **Templates with relative dates and reset state.** Basecamp: creating from a template resets to-dos, returns cards to Triage, drops chat, shifts dates from
    project start. A "new project from this one" must strip session history and ledger entries.
11. **Home page = README + sidebar digest, not a list.** GitHub's repo root (README, latest release, packages, languages) and Linear's Overview (summary, latest
    update, graph). The Board's home should be a rendered `BOARD.md` head plus a computed sidebar: open cards by status, latest run, expired memories, skills count.
12. **Mirrors, not copies, across boards.** Trello mirror cards edit the source in place and wear a badge naming where the source lives. A card shown in another
    project's board should be a reference with the origin badge, never a duplicated file.

## Traps

- **Text autolinks that stop working outside conversations.** GitHub: "Autolinked references are not created in wikis or files in a repository", and code permalinks
  "render as a code snippet only in the repository it originated in… This does not work in Markdown files, only in comments"
  ([autolinks](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/autolinked-references-and-urls),
  [permalinks](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-a-permanent-link-to-a-code-snippet)). A file-based
  board must resolve `#ID` in *every* Markdown file it owns, or people will learn that links only work in some places.
- **Closing keywords with hidden preconditions.** GitHub's `closes #n` is "interpreted only when the pull request targets the repository's default branch"; manual
  linking is same-repo only and capped at ten ([linking
  PRs](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/linking-a-pull-request-to-an-issue)). Document the exact grammar, and make
  failure loud.
- **One-way relations that look two-way.** Notion relations are one-way "by default" and only bidirectional when "Show on [related database]" is toggled
  ([relations](https://www.notion.com/help/relations-and-rollups)); the template docs warn a prefilled relation makes "every page you create with that template…
  relate to the same existing page(s)" ([database templates](https://www.notion.com/help/database-templates)). Store the edge once; derive both views.
- **Copies that silently drop parts of the object.** Trello board copies keep descriptions but "We don't copy comments, card activity, card members or archived
  cards"; editing a card template "won't update other cards created using that template"; template cards hide dates
  ([copying](https://support.atlassian.com/trello/docs/copying-cards-lists-or-boards/), [template
  cards](https://support.atlassian.com/trello/docs/creating-template-cards/)). Any "duplicate card" must say what it left out.
- **Mirrors that are not the thing.** Trello mirrors cannot run Power-Ups or card buttons and are not archived with the source
  ([mirroring](https://support.atlassian.com/trello/docs/mirroring-cards/)); on a free plan the same gesture makes a dead link card. A reference must show the origin
  and fail visibly when the origin is gone.
- **Procedures metered and stored outside the project.** Trello automation runs are pooled per Workspace and quota'd (250/1,000/unlimited per month), buttons capped
  at 20+20 ([quotas](https://support.atlassian.com/trello/docs/butler-quotas-and-limits/)); Notion buttons and Linear templates live in the workspace DB. A skill
  that lives outside the repo cannot be reviewed, pinned or restored with it.
- **Relations stored per item with no view of the graph.** Plane "stores relations per item and has no view of the shape they make, and no plug-in API to add one"
  ([plane-mcp-server issue](https://github.com/MrSampson/plane-mcp-server/issues/25)); Jira caps a work item at 2,000 links ([link a page to an
  epic](https://support.atlassian.com/jira-software-cloud/docs/link-a-confluence-page-to-an-epic/)). Keep an index that can answer "what depends on X" without
  opening every card.
- **Verification that is a paid feature.** Notion's owner/expiry model is Business/Enterprise only ([wikis](https://www.notion.com/help/wikis-and-verified-pages));
  most teams never see it and their wikis rot. Freshness metadata has to be cheap enough to be on by default.
- **Custody.** Height announced in March 2025 and closed on 2025-09-24 with no stated reason
  ([Creativerly](https://www.creativerly.com/height-app-is-shutting-down/)); every artifact, spec and AI-maintained summary lived in its DB. Git-native files are the
  mitigation, provided the *links* are in the files too and not only in an index.
