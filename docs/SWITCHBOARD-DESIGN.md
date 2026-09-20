# Board (the "Switchboard"): design v2 (proposal, 2026-09-17)

Supersedes sections 3–6 of `docs/SCRATCHPAD-DESIGN.md` (single pad file, `### S<n>` items, numbered-answer merge,
`Ctrl+Shift+J`); its section 2 (research) still applies. Nothing here is implemented. Owner request (2026-09-17): a
Ctrl+Shift+S pane of Trello-like cards, each an issue and a thread with the agent, referable from the terminal; tabs
(design, bugs, features, marketing, planning, deferred, done); in git with a Markdown form usable without Relay;
detection and conversion of non-compliant notes; autonomous agent use, iterated through QA.

## 1. Name

Switchboard fits Relay's telecom theme (it routes work between owner, agents and collaborators) but is long in UI,
tool and command names, reads like a settings screen, and the owner suspects it is too cute. Pad/Scratchpad undersells
tracking and QA lanes; Desk is vague; Dispatch collides with `Pane::dispatch` and routing vocabulary; "Relay Board" is
redundant inside Relay. **Recommendation: "Board"** in product, code, tools and docs (`BoardPane`, `board_*`,
`/board`): short, obvious (Trello, GitHub Projects). "Switchboard" only as a website nickname.

## 2. Storage: the board IS `issues/`, with a UI

**Decision: unify.** `issues/` already has the lifecycle, QA lanes, evidence rules and the global `issue-tracking`
skill that every agent (in Relay or not) follows, and it is already Markdown in git; a separate `.relay/board` would
drift from it within a week. The Board adds a UI, card threads, a stricter header, ordering and autonomy rules.

```text
issues/board.yaml        tabs, columns, labels, autonomy (committed config)
issues/BOARD.md          generated index for GitHub/editor readers (regenerated, never hand-merged)
issues/<category>/       features/ changes/ design/ marketing/ planning/ = tabs; open cards at the top level; state
                         subfolders needs_qa_llm/ needs_qa_human/ needs_review/ needs_labels/ needs_ab/ deferred/ done/
issues/threads/<ID>.md   one append-only thread per card (not a category)
```

**Folder = tab and coarse lane; front matter = exact state.** The skill's "move the file to the state subfolder" rule
stays, so agents without Relay still see QA lanes by path. Finer columns (inbox, discussing, ready, in-progress) exist
only in front matter. `relay board check` fails when folder and status disagree.

### 2.1 Identity and merge behavior

- **Id:** 4 random Crockford-base32 characters with at least one letter (`K7Q2`), referenced as `#K7Q2`. Random, not
  sequential, so collaborators on branches never allocate the same number; no all-digit ids, so GitHub does not
  autolink them as `#1234`. A post-merge duplicate is flagged by `check`; the newer card is re-id'd (`aliases: [old]`).
- **File name** stays `YYYY-MM-DD-slug.md`, frozen at creation. **Title** is the body's first `# ` heading (one copy).
  **No `updated` key** (it would conflict on every concurrent edit); last activity comes from the thread and git.
- **Order** is a per-card `rank` string (fractional index). Reordering touches only the moved card; an ordered list in
  a shared index file would conflict on nearly every move.
- **Threads** are separate files with `.gitattributes` `issues/threads/*.md merge=union`. Entries are append-only
  self-contained blocks with sortable ids, so a union merge keeps both sides' appends intact and Relay sorts on display.
  Card files are *not* union-merged (duplicate YAML keys); their rare front matter conflicts show as git markers and a
  "Resolve conflict" banner (ours/theirs per field). A key-wise merge driver comes later.
- **Body vs thread.** The body is the issue document QA sessions read: request, decisions, plan links, implementer
  check, QA checklist, verdicts. The thread is the conversation; the agent distills decisions into `## Decisions`.

### 2.2 Migrating the 43 existing issues

One deterministic commit (`issue: migrate tracker to board format`), no model call: parse `- **Field**: value` headers
into YAML front matter, keep the H1 and every section byte-for-byte, assign ids and ranks, map `open` → `ready`, keep
`needs_qa_llm/` folders (`status: needs-qa-llm`), add `board.yaml`, `threads/`, `.gitattributes`, `BOARD.md`, and
update `issues/README.md`. `docs/VALIDATION.md`'s QA list becomes generated. The global skill gains one paragraph: "if
the repository has `issues/board.yaml`, use YAML front matter and append-only `threads/`" (other repos unaffected).

## 3. Tabs and columns

> **Superseded by 4.6 (2026-09-18):** the pane renders no tabs. `board.yaml`'s `tabs:` are the
> category folders a card's file lives in; its `columns:`/`column_statuses:` are the sections of
> the one list. The column table below still describes the statuses and their folders.

**Tabs are categories; columns are status.** Tabs = status would put "bugs" and "needs QA" on one axis, so a bug
needing QA would live in two places. Deferred and Done are cross-category status tabs.

| Tab (configurable) | Shows | Folder |
|---|---|---|
| Features | open cards in `features/` | `features/` |
| Bugs | bugs and behavior changes (`labels: [bug]` distinguishes) | `changes/` (the skill's name, kept) |
| Design, Marketing, Planning | open cards in their folder | created on first card |
| Deferred | `status: deferred`, all categories, grouped by category | `*/deferred/` |
| Done | `done` and `dropped`, all categories, newest first, searchable | `*/done/` |

| Column | `status` | Folder | Notes |
|---|---|---|---|
| Inbox | `inbox` | top | raw capture, untriaged |
| Discussing | `discussing` | top | `waiting_on: owner \| agent \| <person>` badge |
| Ready | `ready` (legacy `open`) | top | agreed, not started |
| In progress | `in-progress` | top | `assignee` set (person or agent) |
| Waiting | `needs-review`, `needs-labels`, `needs-ab` | `needs_review/` … | chip names the kind; not landing states (skill rule 6) |
| Needs QA | `needs-qa-llm`, `needs-qa-human` | `needs_qa_llm/`, `needs_qa_human/` | two swimlanes, LLM and Human |
| Done | collapsed (last 5) in category tabs | `done/` | resolution section required |

A QA reopen is not a status: the card returns to Ready with label `reopened` and the fault note appended.

## 4. UI

### 4.1 Opening

- **Ctrl+Shift+S** = `board.open`: opens the Board beside the anchor terminal (split right) or focuses the tab's
  existing Board; pressed on the Board, it returns focus to the last terminal pane. **Conflicts:** none in Relay
  defaults (Ctrl+Shift+ A, H, P, R, Tab, W, X are taken). VS Code (Save As) and Konsole (Save Output As) use it in their
  home apps (*verify in KEYBINDING-PRESETS.md*; Warp unverified), but Relay has no such actions, so bind it in all four
  presets. With `program_keys: shift-only` it reaches Relay inside programs, which cannot distinguish it from Ctrl+S.
- Palette `board.open` ("Board"; aliases switchboard, issues, cards, todo, scratchpad, trello), a Board button in the
  tab-bar row, `/board`, `/card <text>` (quick-add to Inbox without opening), `relay board` in a shell.
- Layout node `{"board": {"workspace", "tab"}}`; workspace = git root of the anchor cwd. Without `issues/board.yaml`
  the first open offers "Create board in issues/" or conversion of an existing tracker (section 7).

**Which project a tab is attached to** (owner, 2026-09-18, card #JN7X; `src/Projects.h`). A **tab is attached to at
most one project, and starts attached to none** — the quiet state, and the normal one: no chip, no hint about the
board, no offer, and the tab's panes get no board tools and no board policy in their prompt (`configure`'s
`board {attach: false}`, protocol 19.1). Most of the time the terminal is standing in `~/Downloads` or an admin folder
and there is no project to talk about.

- A pane's **candidate** project is `relay::projects::candidateFor(<the pane's live terminal cwd>)`, derived fresh
  whenever it is needed and never cached on the pane: the nearest ancestor with a board, else the nearest with `.git`.
  A candidate is an offer, not an attachment.
- **Terminal commands never attach.** Only an explicit project action does: opening the Switchboard, `/card`, picking
  a card with `#`, Execute-from-card. Each is one of the closed set of reasons in `src/Projects.h`, and that reason is
  what the known-projects registry records.
- Attachment is **sticky until detached**, and a tab never switches project silently: a pane that has `cd`-ed into
  another checkout says so ("This tab's Switchboard is A; … belongs to B") instead of re-pointing. Detaching is the
  palette's "Detach this tab from <project>", offered only while the tab is attached; it closes nothing — an open
  Switchboard stays open on its board — and the panes simply lose the card tools.
- Attaching and detaching send `set_board` (protocol 19.11) to every pane in the tab, so an agent **gains or loses the
  card tools without losing the conversation it was in the middle of**.
- A project's board is `<project>/switchboard/` (new) or `<project>/issues/` (an existing tracker, kept where it is),
  and **it is only ever created after the user answers "Initialize a project and create a Switchboard here?"**
  (protocol 19.12). Until then an attached project sends `board {project, state: "uninitialized"}` and a candidate with
  no board gets one quiet status line on Ctrl+Shift+S and `/card` — nothing is written anywhere.
- The layout saves an attached tab as `{"project": "…", "node": <tab node>}` and an unattached one as the bare node it
  always was, so there is no schema bump (`relay::windowstate::tabNode`/`tabProject`). A restored tab comes back
  attached when that directory is still there, and unattached and quiet when it is not.

### 4.2 Board view (`BoardPane`, a `ToolPane` leaf)

> **Superseded by 4.6:** one scrolling list of rows, sectioned by status, with no tab row.

Top: tab bar with counts, filter field, label chips, Mine / Agent / Waiting-on-me toggles. Body: horizontally scrolling
columns; a card shows `#ID`, title (2 lines), labels, assignee (✦ = agent), `waiting_on` badge, thread count and an
unread dot (tracked locally in QSettings, not git). Drag between columns (status + folder move) or onto a tab (category
move). Quick add: `+` or `n` opens a growing field; Enter creates the card with the text verbatim as `## Issue`.

### 4.3 Card detail

Enter opens it in the right half of the Board (Esc closes; Shift+Enter opens its own pane). Header: title, status and
tab pickers, labels, assignee, links (plans, commits, evidence, related). Body: rendered Markdown; `e` edits
(`PlanEditor`, Ctrl+S, atomic, hash-checked). Thread: entries with author and time; the agent's reply streams;
thinking collapses to "Thought 9 s"; tools collapse to one `turn_summary` line ("⚙ 3 tools · 12 s") expanding via
`turn_transcript_get` (`TurnTranscript`). Reply box: `RichEditor` with composer keys and `/plan`, `/move`, `/assign`.

**Which agent answers:** a dedicated **board worker** per board root per window (an ordinary `worker.py` with its own
queue), so card chats never pollute a pane's conversation. The Switchboard is per project: a window showing two
projects' boards runs a worker for each, keyed by the board root, and each worker's events reach only the Switchboard
views of that root. The worker is started with the first Switchboard opened on a root and stopped when the last one in
the window closes. Which project a pane's board is comes from that pane alone — its **live terminal directory** and
nothing else (`relay::boardRootFor`, `src/BoardWorkspace.h`, walking up to `/` and trying `switchboard/board.yaml`
then `issues/board.yaml` at each level, so the nearest ancestor wins whatever its folder is called). There is
deliberately no window-wide or process-wide fallback and the pane's own `workspace()` is not a candidate either: all
three are the directory Relay was launched in, which is how one project's board reached every pane. It is stateless between turns: each turn is seeded from the body plus the
thread (older entries summarized past a cap), so the *file* is the memory and a collaborator's Relay continues the same
thread. Its model chip defaults to the anchor pane's preset.

### 4.4 Keyboard and hints

`board.*` Keymap actions, active when the Board has focus: arrows select; Enter/Esc open/close; `n` new; `m` move
(popup, digits pick a column, Tab lists tabs); Alt+Shift+Left/Right move a column, Alt+Shift+Up/Down reorder; `/`
or Esc filter (`label:`, `status:`, `@agent`, `waiting:me`, text) — on the main page Esc lands in the filter bar,
taking an active filter off first (#K9X6); `l` labels; `a` assign; `c` reply; `t` insert `#ID` into the
anchor composer and focus it; `y` copy `#ID`; `o` open the file in a preview pane; Ctrl+PgUp/PgDn switch tabs; `?` keys.
**Hints** (WARP.md rule, live Keymap text): palette/button → Ctrl+Shift+S; mouse drag → `m`; clicking `+` → `n`;
"Send to terminal" button → `t`; typed `/board` → the open shortcut; clicking into the filter → `Esc`.

### 4.5 As built (UX pass, 2026-09-17)

> **Superseded by 4.6** where the two disagree: the columns became one sectioned list and the
> tabs went away. Everything 4.5 says about moving, notices, in-place updates, quick add and the
> card detail still holds.

What the pane did after that pass where it differs from, or fills in, 4.2–4.4 (`src/BoardPane.cpp`, styles in `src/Theme.cpp`).
Evidence: `docs/qa_evidence/2026-09-17-switchboard-ux/`.

- **Chrome.** Two rows: the tabs (with counts) get the full width, then the filter and **+ New card**. The pane's hover
  buttons reserve their room at the right of the tab row, as a terminal pane's header does. Format problems are one
  warning line that links to the file. A key line at the bottom says what the keys do on what is on screen.
- **Cards** are painted boxes: title up to 3 lines, then `#ID` and badges (labels, ✦ agent or assignee,
  `waiting: …` in the warning colour, `☑ done/total`, `✎ thread`). Columns that hold several statuses (Waiting, Needs
  QA) add a short status badge (`LLM QA`, `human QA`, `review`). Columns are `@surface` strips with engraved headers
  (SWITCHBOARD-AESTHETIC intervention 3) and a `+` that adds into that column (not on Done).
- **Moving.** A click opens a card; a drag shows the target column tinted and a line where the card lands, scrolls the
  board and the column when held near an edge, and may drop on a tab (category move). A drop where the card already
  was writes nothing; a reorder within a column keeps the exact status. Every move from the pane shows a notice that
  floats over the bottom of the board (it never shifts the cards) with **Undo**, also **Ctrl+Z**; a refused move
  (Needs QA without evidence) shows the worker's reason. Dragging a card onto the prompt box types its `#ID`.
- **Updates in place.** A change inside the current tab refills the columns: scroll positions, focus, the selection
  and an open quick-add field survive, and a card moved by keyboard keeps the focus in its new column. The watcher's
  refresh after one's own write is ignored when nothing changed. The open card re-reads only when *its* file changes.
- **Quick add** stays open after Enter for the next card; Esc or leaving it empty closes it. An empty tab says so once.
- **Card detail** is one document: the body (its leading `# Title` dropped when it repeats the header, headings at a
  panel scale), then the thread (events as one muted line, comments with author, model, kind and age). Below
  ~900 px of pane it takes the whole pane (Esc goes back); wider, it sits beside the columns and follows the
  selection. Each card keeps its own unsent reply. Enter asks the agent, **Ctrl+Shift+Enter** only comments (the
  composer's "never the model" chord); while it answers, the button is **Stop** (`cancel`). An ask the agent cannot
  take (no key) is reported on the card; the question is already in the thread. The header has **#ID → prompt** and
  **Open file**; links in the body open the file they name.
- **Not built** from 4.2–4.4: label chips and Mine/Agent/Waiting toggles (the filter language covers them), the
  unread dot, `l`/`a`, `?`, Shift+Enter "own pane", thinking/tool collapse in the thread, tickable tasks.
  (`e` edit was not built either until 2026-09-18; see 4.8.)

### 4.6 One list, sectioned by status — no tabs (owner decision, 2026-09-18)

Supersedes 4.2's columns, 4.4's column keys and the tab row in 3. Evidence:
`docs/qa_evidence/2026-09-18-switchboard-rows/`. Code: `src/BoardPane.cpp`, the layout-independent
half in `src/BoardModel.cpp` (`Model::sections`, `Model::rows`, `dropTarget`, `stepRow`,
`fitBadges`, `badges`), styles in `src/Theme.cpp`.

**Why.** With ~96 cards the Trello columns ran off the right edge and wasted the height: seven
narrow strips, each scrolling on its own, and a card had to be hunted for. A single-owner tracker
reads better as rows. The owner then went further: the tab set (Features, Bugs, Design, Marketing,
Planning, Deferred, Done) mixed three different ideas — type, area and status — and GitHub Issues
has no tabs at all. So:

- **One view.** The pane is one list of every card that is not done or dropped. There is no tab
  row; the counts live in the pane's title (`Switchboard · 84 open`) and beside the filter box.
  *(4.7: that filter box moved out of the pane's header and became the top of the list page.)*
- **`bug` and `feature` are labels**, like `voice` or `design`: a badge on the row and
  `label:bug` in the filter box. Not tabs, not folders-as-tabs, not a card type. The front
  matter's `type` (work/plan/memory) and the folder layout on disk are unchanged; the UI is simply
  not driven by them, and `folder:changes` (or `folder:bugs`, the board.yaml id that names it)
  reaches those cards.
- **Done is a status, not a place.** Done and dropped share the last section, which starts folded
  and shows its count; `status:done` and `status:dropped` in the filter box find them. Deferred is
  likewise a section (also folded by default), not a tab.
- `issues/board.yaml` is untouched. Its `tabs:` are still the category folders a card's *file*
  lives in — the choices in the card detail's second picker and in the `m` menu — and its
  `columns:`/`column_statuses:` still decide the sections.

**Sections.** The configured columns in their configured order, then a section for any status they
do not collect (a plan's Draft/Approved/Executing, a memory's Active, a Deferred card), then Done.
So one list really does hold every open card whatever its type. A header carries the status name
and its count, folds on a click (or Left/Right), and has a `+` on hover that adds into it; which
sections are folded is saved with the window's layout (`{"board": {"workspace", "collapsed"}}`).
A checkbox at the top of the page (4.7) takes a whole section off the list, which is a different
thing from folding it. **The order of the sections is the board's own** and is set in the gear
(owner, 2026-09-19: "we do need sorting of sections though. enable those to be dragged and
dropped, with up and down buttons for moving them, in the section settings modal"): each row of
the section list has a drag handle and ▲ ▼ buttons, moving one is one rewrite of `columns:` in the
new order, and Verified and Done stay the last two — nothing moves past them and they do not move
themselves (`SectionPlan::move` / `moveBefore`).

**A row.** The priority flag, then the `#ID`, then the title (elided), then labels / `✦ agent`
or assignee / `waiting: …` / `☑ done/total` / `✎ thread`, right-aligned. The status glyph column
is gone (owner, 2026-09-20, #VKFV: "those icons arent useful because they just reflect sections");
its place is the **flag** — an empty ring at priority 0 (the default), a yellow disc at −1, white
at +1, pale green at +2, bright green at +3, as `[board]` theme tokens — and a **left click on it
raises the card's priority, a right click lowers it** (clamped at −1…+3; `board_priority`,
protocol 19.3; Ctrl+Z undoes a click). While a turn runs on the card the flag is replaced by the
agent's ✦, the one mark a running turn leaves on a row. The `#ID` moved with it (owner, #VKFV:
"the # code as a second column before the title"): a fixed mono column the width of `#WWWW`, so
every id lines up under the header whatever its title says. The table's two right-hand
columns are **Created** and **Updated** (owner, 2026-09-19: "add a 'created' and 'updated'
column"; the age badge that sat left of Created went with the glyph, #VKFV — three date-like
columns read as two too many), each a fixed mono cell holding the date part of the card's
`created`/`updated` (`2026-09-19`), with the badges to their left and the header's labels over
them; a cell a card has nothing to say in stays blank, and both columns go when the pane is too
narrow for them (the same question the header asks, so a label never outlives its cells). The
title is owed 45% of the row (80–280 px) before a badge may have anything, no single badge may
take more than a quarter of the width, and the badges that do not fit are dropped in order —
labels, thread count, tasks, assignee, agent, private, status, `waiting:` — so a narrow pane loses
decoration before it loses meaning (`board::fitBadges`, tested). At 390 px the title still elides
rather than the row wrapping.

**Sorting.** The list's own column header (owner, 2026-09-19: "change switchboard sorting from a
sort button to adding header columns that you click on ... and sorting is within section"): a row
of cells over the list — **⚑**, **Card**, **Created**, **Updated** — each one a click. The ⚑
(#VKFV) is the priority flag's own cell, one glyph wide over the flag column: a click orders by
**priority high first** (ties keep the section's own order), a second by **priority low first**,
and the accent colour says it is on, because no arrow fits beside the glyph. A click orders the
cards *inside every section* by that column, a second click turns that column round, and a third
gives the board its own order back, so the drag order is always one click away and no control has
to say which order is on: the cell that is on wears the arrow (`▲` for oldest first, A→Z and least
recently updated; `▼` for the other way). The orders are **Manual** — the board's own rank, the
order drag and drop and Alt+Shift+↑↓ write, with Done and Verified newest first as they always
were — **Newest first** and **Oldest first** (by `created`), **Recently updated** and **Least
recently updated** (by the row's `updated`, the card file's or its thread file's mtime, whichever
is later; an older worker sends none and those sorts fall back to `created`), and **Title A→Z** and
**Title Z→A**. Any order but Manual takes the manual reorder off — a drop inside the card's own
section and Alt+Shift+↑↓ answer with a notice pointing back at the header, while drops *between*
sections still move, because they write a status and not a place. The choice is saved with the
window's layout (`{"board": {"workspace", "collapsed", "hidden", "labels", "sort"}}`) and each pane keeps its
own. The two date cells are the row's right-hand columns (below), and they go, labels and all,
when the pane is too narrow to carry them.

**Keyboard.** Up/Down walk the card rows of the whole list, stepping over the headers; PageUp/Down
and Home/End likewise; Enter opens; Left folds the selection's section and stands on the nearest
card still on screen; Right unfolds the folded section nearest the selection (so Left and Right
undo each other, and at the end of the list Right opens Done) and stands on its first card;
Alt+Shift+Up/Down reorder inside the section; Alt+Shift+Left/Right move the card to the previous
or next status; `n` adds to the section the selection is in; `/` focuses the filter. `m`, `c`,
`t`, `y`, `o` and Ctrl+Z are unchanged. Ctrl+PgUp/PgDn no longer switch anything (there are no
tabs). The key line at the bottom says exactly this, and switches to the card's keys when a card
has the pane to itself.

**Filtering.** The filter box filters rows live across every section: a section with no match is
left out, the counts follow, and nothing is folded while a filter is active — a search that hid
its own matches would be a search that does nothing. The tokens are `label:`, `status:`,
`folder:`, `@assignee`, `waiting:`, `#ID` and words. A word is full-text search (2026-09-19): it
matches the row's own fields and the card's whole text — body and thread, which the worker sends
on each row as `text`, capped at 64 KiB (protocol 19.2) — so a phrase remembered from the
request, a decision or a comment finds its card.

### 4.7 The tools are the top of the list page, not the pane's header (owner, 2026-09-18)

> Owner: "put 'new card' and filter at the top of the main org page, not in the pane header. they
> shouldn't show when you are clicked on a card. when you are clicked on a card the header should
> say '← back to board'." And: "at the top of the base page, there should be filter checkboxes at
> the top for the different sections." Evidence:
> `docs/qa_evidence/2026-09-18-board-list-page-tools/`.

- **The header holds one thing, and only sometimes.** `#boardHead` is hidden while the list is on
  screen. A card open in a pane narrow enough to stack (< 900 px, where the card takes the whole
  pane) puts **← Back to board** there, which does what Esc does and hints Esc when it is clicked.
  Nothing else is ever in that row.
- **The list's tools belong to the list page.** The count, the filter box, **+ New card** and
  **Clean up** are `#boardListTools`, the first widget *inside* `#boardListPane`. They are
  therefore present exactly when the list is, and an open card that has the pane to itself does
  not carry the list's controls at its head. In a wide pane, where the list stays beside the card,
  its tools stay with it — they are the list's, not the window's.
- **A checkbox per section**, under that row, ticked by default, engraved in the same uppercase
  mono as the section headers they switch. Unticking one takes the section off the page — header,
  cards and count — and it composes with the text filter rather than being overridden by it.
  Unticked is *not* folded: a folded section is a header with its cards put away and its cards
  still counted; an unticked one is not on the page. The count label says so
  (`62 of 84 open`), and the tooltip gives the number each box is holding. They wrap onto further
  lines in a narrow pane, so eight or nine sections fit at ~350 px.
- **A chip per label, under the checkboxes** (owner, 2026-09-20, #VKFV: clean up annotates
  `bug`/`feature`/area labels and "those should become a second set of filter next to the section
  list"): one lower-case chip per label the board carries — every label on a card plus any the
  board's own config names, rebuilt when that set changes. Ticking keeps only the cards that carry
  **every** ticked label, exactly what `label:` terms do, composing with the text filter and the
  section checkboxes; the count label switches to "N shown" as it does for the filter box. A label
  that goes away while ticked simply stops filtering.
- **All three sets are saved with the window's layout**, side by side:
  `{"board": {"workspace", "collapsed", "hidden", "labels"}}` (`BoardView::collapsedSections()`,
  `hiddenSections()` and `labelFilter()`).
- **Clean up** (`#boardCleanup`) hands the board to the agent to tidy: merge or split sections and
  cards, review statuses. Protocol 19.9; see 4.8. Since #VKFV its brief (v2) also annotates labels
  — every work card gets `bug` or `feature` plus the area labels the board already uses — and
  *suggests* a tag outside the vocabulary in its report instead of writing it, because the chips
  turn every label into a filter.

### 4.8 The cleanup button: preview, then apply (2026-09-18)

`board_cleanup` (protocol 19.9) is one agent turn over the whole board that rewrites many of the
owner's files. The pane never starts one of those from a click.

- **The first click is a preview.** `BoardView::requestCleanup()` sends
  `board_cleanup {dry_run: true}`: the agent makes the same plan and every write tool refuses, so
  the run produces proposals and changes nothing. The result panel's **Apply** is the only path to
  a run that writes. No dialog asks "are you sure" — a plan the person can read is a better
  question than a modal, and the owner's rule is that a surface is a pane or in-pane, never a
  floating strip.
- **While it runs the same button is Stop**, in the warning colour, and sends the ordinary
  `cancel` — mirroring the card detail's Ask/Stop. A run takes minutes, so progress is a line in
  the board's notice area that stays up instead of timing out: elapsed, the tool or step it is on
  or the last `board_activity`, a running count of what it has written or proposed, and "nothing is
  written" while it is a preview. With a card open in a stacked pane the notice moves to the *top*
  of the pane, because the bottom is where that card's reply box and Ask button are.
- **A cleanup's events never reach a card thread.** They carry `cleanup: true` and a `run_id` and
  no `card_id`, and `BoardView::handleCleanupEvent()` takes every one of them before the card
  routing runs.
- **The result is a panel in the list page** (`#boardCleanupPanel`), under the tools and over the
  rows: the outcome, whether anything was written, the counts, every change with its `#ID` as a
  link that opens the card and its file as a link that opens the file, the refusals, the agent's
  report, a **Changelog** button that opens the run's Markdown through `onOpenFile`, **Apply**
  after a preview that found something, and **Dismiss**. Every control in it is `NoFocus`, so the
  arrows still walk the list.
- **Busy** (19.9: the worker runs one turn at a time and the two refuse each other). A card's
  Discuss or Plan (until 4.9, "Ask the agent") pressed while a cleanup runs is stopped in the pane and explained on the card,
  with the typed message put back in the reply box rather than sent away to bounce; a
  `board_busy` from the worker says which is running — "A cleanup is running on this board." or
  "The agent is answering on #K7Q2." — in the notice, or on the card when it was that card's ask.
- **No key and no palette entry**, so the WARP.md hint rule has nothing to register: a cleanup is a
  rare, minutes-long, board-wide write, and a shortcut for it would be a way to start one by
  accident. If one is ever added, the hint goes with it.

**Quick add** is a field over the list rather than inside a section: with one long list, a field
at a section's head would be scrolled out of sight as often as not. It names the section it adds
to, Enter adds and keeps it open for the next card, Esc or losing the focus while empty closes it.
With no tabs there is no "current" category, so a new card is filed in the first category folder
`board.yaml` names (features) and `m` re-files it.

**Drag and drop** is unchanged in behaviour: a drag shows the row on the board's background, a
2 px accent line where it would land between two rows, or the whole section header lit when the
pointer is on one (including a folded one — the card lands at its top); the list scrolls while a
card is held near an edge; a drop where the card already was writes nothing; and a card dropped on
the prompt box types its `#ID`. Dropping a card on a tab is gone with the tabs.

**Unchanged from 4.5:** the floating notice with **Undo** (and Ctrl+Z), the refused-move reason
shown where the card was dropped, the in-place refill that keeps the scroll position, the
selection, the focus and an open quick-add field, the problems line, and the card detail (full
pane below ~900 px of pane, beside the list when wider, per-card unsent replies,
Ctrl+Shift+Enter comments, Ask/Stop).

**Found on the way.** The worker's row never carried `created`, `tasks_done`, `tasks_total`,
`milestone`, `topic` or `implemented_by`, although protocol 19.2 promises them, so the pane's age
and `☑ done/total` badges had nothing to draw. `board_tools._row` now sends the full row.

### 4.8 A card's own words are editable, and they are the Issue (owner, 2026-09-18)

Owner: *"after adding a card, i couldn't edit the title or the task. i'm not sure about 'request'
there, let's call it issue."* Both halves of that. Evidence:
`docs/qa_evidence/2026-09-18-edit-card-title-and-issue/`.

**Editing was never built.** 4.3 planned `e` (a `PlanEditor`, Ctrl+S) and 4.5 listed it under "not
built"; the card detail was a label and a read-only `QTextBrowser`, so a card added by quick add
could only be changed by opening its file. The *worker* could already do it — `board_update`
(protocol 19.3) with `board_update_card`'s `title` and `replace_section`, hash-checked, with the
old text kept in the thread (decision 12.3) — so this is a pane that never asked.

- **What is edited:** the title and the `## Issue` section, together and in one write, because
  they are one thought. Other sections (Tasks, Decisions, a QA checklist) belong to the agent and
  to the QA lane; they are still edited in the file.
- **How it starts:** the **Edit** button on the card, a click on the title, a double-click in the
  text, or `e` (on the list it opens the card and starts editing). The shortcut hint fires on the
  three slow paths, per the WARP rule.
- **How it ends:** Enter in the title or Ctrl+Enter in the text saves (Enter there is a newline —
  the reply box's Enter-sends is for one thought, not a paragraph); **Save** does the same; Esc or
  **Cancel** puts the card back exactly as it was and writes nothing. Saving with nothing changed
  writes nothing either.
- **While editing** the card is not swapped for another one by the selection, the reply box stands
  down, and the document is the editor — a card is being read or being written, never both.
- **A file that changed under the edit** is never silently overwritten: the save carries the hash
  the card was read at, so the worker refuses it (`board_conflict`), the pane re-reads the card,
  keeps what was typed and says that a second Save writes over the new version. The thread keeps
  the old text either way, so even that is reversible.

**Request became Issue.** `## Issue` is the section's name everywhere new text is written: quick
add, `board_create_card`, and any `replace_section` naming either spelling. Readers accept both
(`relay_core.board.ISSUE_HEADINGS`), so the ~96 cards already filed keep saying `## Request` until
something edits them — there is no rewriting commit, and `check` does not care which a card says.

### 4.9 Discuss, Plan, Execute instead of "Ask the agent" (owner, 2026-09-18)

Owner (#XS6Q): *"rather than "ask the agent", lets have: plan / edit / discuss"*. Decided as three
buttons under a card's reply box, after **Comment**: **Discuss** (the accent button), **Plan**,
**Execute** (outlined in the agent colour: it leaves the board). Protocol: 19.10. Evidence:
`docs/qa_evidence/2026-09-18-card-discuss-plan-execute/`.

- **Discuss** is the old ask, and it edits: the agent may retitle the card, rewrite its `## Issue`,
  relabel it or move it when the conversation calls for it, through the same hash-checked
  `board_update_card` / `board_move_card` as always, so the thread keeps the old text (`rewrite`
  entries) and an event line per change, and the answer says what it changed. The owner's own
  editing is `e` (4.8), unchanged. **Enter** in the reply box discusses.
- **Plan** has the agent read the code (read-only: `read_file`, `list_directory`, and
  `search_files`, which exists for this) and write or revise the card's `## Plan`. It touches no
  code and no other card, and the tools refuse if it tries. Words in the reply box go with it as the
  owner's note; an empty box is fine. **`p`** on the card or the list, or **Ctrl+Enter** in the reply box.
- **Execute** hands the card to a new terminal pane split beside the board, in the board's
  workspace, on the main agent: the card goes to In progress and to the agent, a progress note goes
  in the thread, and the pane's agent gets the card attached with a task that tells it the board's
  conventions (`implemented_by`, `#ID` in every commit message, the hashes in `links.commits`, the
  QA lane when it lands). A card with neither a plan nor an `acceptance` line asks once, on the
  card, in the error line under it ("Execute again (x) … or Plan (p) first") — no dialog. **`x`**.
- **One turn at a time, per card as before.** While a Discuss or a Plan runs the other modes are
  disabled and the agent's streaming reply is headed with the mode. *(Until #VZ69 the running
  mode's own button became **Stop**; since 4.12 it is the strip over the reply box, and Discuss and
  Comment have no buttons at all.)* The cleanup/busy interplay of 4.8 applies to both modes.
- **The thread names the mode** on every entry that has one: "owner  Plan · 2 min ago",
  "✦ agent  Discuss · glm-5". Entries from before carry no mode and read as they did.
- **Keys** (card view): `e` edit, `d` or Tab to the reply box, Enter discuss, `p` / Ctrl+Enter plan,
  `x` execute, Ctrl+Shift+Enter comment only. On the list, `p` and `x` open the selected card and
  do the same. A click on a button shows its key once (WARP.md hint rule; hint ids `board.plan`,
  `board.execute`, `board.verify`, and `board.edit` for the pencil. `board.discuss` went with the
  Discuss button in 4.12: Enter *is* the fast path, so there is no slow path left to teach).
- **Before this**, the card's ask ran on a worker with every pane tool, so "ask the agent" could run
  commands and write files from a card thread. Discuss and Plan now cannot; that is Execute's job.

### 4.10 Verify: the card names its cross-provider verifier (#T71W, 2026-09-19)

A card in a QA lane says *who* should check it, and one key opens them. The recommendation is the
worker's (`backend/relay_core/qa_verifiers.py`): the ranking, the lineage rule and what is actually
installed or keyed on this machine are all its business, and it arrives with the card as a `qa`
object on `board_card_get` (its shape is in the card, #T71W, under "Where it shows"). The GUI only
reads it, so the order can change without touching a widget. Protocol: 19. Evidence:
`docs/qa_evidence/2026-09-19-cross-provider-qa/`.

- **The line** sits under the fields, in the same muted ink, and only while the card's status is
  `needs-qa-*`: *"Verify with Codex (installed) · then GLM-5.3 · Claude skipped: implemented this
  card"*. The parenthesis is how that verifier is reachable here — `guest:` is a CLI on PATH
  ("installed"), `preset:` a stored key. With nothing available it reads *"No verifier available:
  Claude skipped: implemented this card · Codex: not installed · Kimi: no key"*, so the reader knows
  what to install rather than only that the button is dead (`board::verifyLine`).
- **The worker's `note`, in amber.** `recommend()` sends one line of its own when the answer needs
  explaining, and the card shows it in `theme::Warning` — Relay's "a human should look" ink,
  never a colour of this feature's own. With **no** recommendation the note *is* the line and the
  whole line is amber, which is the Relay Free case (owner, 2026-09-19: *"relay free is never used
  for verifying — so verifying is not available on the free plan"*): *"No verifier available.
  Verifying is not available on Relay Free: add a provider key, or install Codex or Claude Code."*
  Verify is then disabled and its tooltip says that same sentence. With a recommendation the muted
  line stands and the note follows it in amber as a warning — a same-lineage verifier, or a local
  model below the floor for judging code — and Verify still works: a weaker check is still a check.
- **Verify (v)** stands beside Execute, in the same agent outline — it also leaves the board — and
  is on screen only in a QA lane, enabled only when a runner exists.
- **What it writes.** One `board_comment` of kind `progress`: *"Verify · handed to a new terminal
  pane on Codex · <why the ranking chose it>"*, with the reply box's words after it as the owner's
  note. **No status change and no assignee change**: the card stays in its QA lane until the
  verifier's own verdict moves it, and the implementer stays the implementer.
- **The pane.** A `preset:<id>` runner is an ordinary Relay agent pane created on that preset
  (`createPane {preset}`) and handed the brief with `startBoardTask`, so the card travels with it as
  `ask {cards: [id]}`. A `guest:<id>` runner is Claude Code or Codex through the same
  door the model picker uses (`Pane::startGuestBoardTask`, owner 2026-09-19: "with verify, it opened
  the codex cli, not our wrapper"): when the worker can run the guest through its harness (protocol
  29.4) the pane goes onto the `guest:<id>` preset and the brief is its first `ask` with the card
  attached, exactly like a preset runner; only when it cannot does the guest's own TUI start in the
  pane's shell with the brief as its **first positional prompt** (both CLIs take one, and
  `relay_core.guest_launch` passes a launch's `extra` through after the flags). A pane that was just
  created does not know which until the worker's presets arrive, so the task waits for them. A
  guest has no `board_*` tools on either route, so the brief says where the card and its thread
  live and what to do without them.
- **The brief** (`board::verifyTask`): read the card and its `## QA checklist`, run every item and
  write down what was actually seen, put the evidence under the card's
  `docs/qa_evidence/<date>-<slug>/` in files named `qa-…`, write `## Verdict` with
  `board_update_card`, then `board_move_card` to `done` or back to `in-progress` with the failures
  on the thread, and sign the evidence commit `Verified-By: <provider/model>`. **Never fix the code
  yourself** — a verifier that edits the code becomes its implementer and the card would need
  verifying again; what it finds goes on the thread or into a new bug card.
- **The other half of the signature.** The Execute brief asks for an `Implemented-By:` trailer on
  every commit for the card, and says the card's own `implemented_by` is stamped by the board, so no
  agent is asked to type a signature it can only guess at.
- **Both briefs ask for the exact model** (owner, 2026-09-19: *"lets try to record the model
  used"*): `<vendor>/<your exact model id>`, the vendor of the *model* and the id actually running,
  not the family. A guest appends the harness that ran it — `anthropic/claude-opus-5 via
  claude-code`, `openai/gpt-5.6-codex via codex` — and falls back to `anthropic/claude-code` or
  `openai/codex` alone only when it cannot see which model it is, which is all that was ever
  observable from outside. The pane cannot fill the model in for the guest: the pane that will run
  the brief does not exist when the brief is written, and the guest's model is only observable once
  it has started, so the brief asks in words instead.
- **A guest verifier writes its own `verified_by`.** Nothing stamps it for a CLI with no `board_*`
  tools, so the Verify brief tells it to put the same signature in the card's front matter when it
  closes the card. Without that the card would land in Done rather than Verified.
- **Keys**: `v` on the open card and on the list (which opens the card first), the hint
  `board.verify` on a click, and `v` in both key legends.

### 4.11 The Verified section (#T71W, owner 2026-09-19)

Owner: *"so we need a Verified section in the switchboard?"* — yes, and **derived, not a status**.
A card whose status is `done` and whose row carries a non-empty `verified_by` sits in **VERIFIED**,
between NEEDS QA and DONE; DONE keeps what is left, which is a card closed without a cross-model
check and every dropped one. No `board.yaml` names it and no card's status is ever "verified":
`Model::sections()` inserts the section before Done whatever the config says, and
`sectionForCard()` is the one place that reads the signature, so every count, row, filter and
checkbox agrees by construction.

- **The section carries no statuses.** That is what keeps it honest: `sectionIndex` never learns
  that `done` lives there (or Done would collect nothing), and `dropStatus("verified")` answers
  nothing, so quick add does not offer it and a drop cannot land in it.
- **Nothing moves in.** A drag or `Alt+Shift+→` aimed at Verified is refused in the pane, with the
  one way in on the notice line: *"A card is verified by closing it from a QA lane with a different
  model."* Reordering *inside* Verified is ordinary, and moving *out* of it behaves exactly like
  moving out of Done.
- **The row says who.** A `✓ <verifier>` badge in the success green, from
  `board::signatureLabel(verified_by)`: *"✓ Codex"*, *"✓ GLM-5.3"*, *"✓ Claude Opus 5 · Claude
  Code"*. In the Verified section every row has one, so the header cannot say it and the badge must.
  The card detail's fields line shows the raw signature, `verified by openai/codex`, beside
  `implemented by`, because on the card the exact string is the point.
- **Its fold and its checkbox are ordinary.** It is not folded by default (Done and Deferred still
  are): the owner asked for the section in order to see it.

### 4.12 The card reads as one page: a pencil, a seam, and a box that does the talking (#VZ69, owner 2026-09-19)

Owner, on the card detail as 4.9 left it: *"there should be a promponent pencil edit button, rather
than the small 'edit' button at the top"*, *"there should be a clearer dematcation between the issue
and the convo thread"*, *"remove comment / discuss buttons. i would say you just press enter in the
prompt box to discuss / comment"*, *"'stop' button isnt intuitive, it should be stop planning i
guess, or there should be an X next to 'agent planning'"*, and on quick add: *"when you first press
enter to add a new card, it should open the edit box, the editable issue part. the first thing you
enter in teh top row thing makes the title, not the issue content."* Evidence:
`docs/qa_evidence/2026-09-19-switchboard-card-ux/`.

- **The pencil is on the title.** `✎ Edit (e)` moves out of the row of muted text buttons at the top
  of the card and sits at the right of the title line, outlined in the accent
  (`QToolButton#boardEditPencil`). It is the only control on a card that is styled as obviously
  pressable, because it is the one the owner reaches for most; `#ID → prompt (t)`, `Open file (o)`
  and the close cross stay quiet text at the top, where they were. Clicking the title and
  double-clicking the text still open the same editor, and `e` still does it from the keyboard.
- **The thread opens on a seam.** The card's own words and the conversation about them are two
  surfaces: the `THREAD · n` heading sits on a ground of its own the width of the document, with a
  hairline rule above it (`insertRule()`, a two-pixel block in the border ink — `QTextDocument` has
  no themeable rule of its own, and a row of box characters would wrap and be copied with the
  text). Bolder ink alone was what the owner could not see.
- **Discuss and Comment have no buttons.** They are what the box does: **Enter** discusses,
  **Ctrl+Shift+Enter** leaves a comment with no model call, and the placeholder says both. The row
  keeps only what is *not* typing into the box — **Plan (p)**, **Execute (x)** and, in a QA lane,
  **Verify (v)**. The empty-thread line teaches the same three keys instead of naming buttons.
- **Stopping a turn is a strip, not a mode-swapped button.** While a turn runs, a line over the
  reply box reads `✦ Switchboarding · planning…` or `✦ Switchboarding · discussing…` with `✕ Stop planning` /
  `✕ Stop discussing` at its right (`boardBusyStrip`, `boardBusyLabel`, `boardStop`); the buttons
  that would start another turn are disabled and keep their own labels. This replaces 4.9's rule
  that the running mode's button becomes **Stop** — which only worked while that mode *had* a
  button, and Discuss no longer does. `cancel` is sent exactly as before.
- **Quick add makes a title and then asks for the issue.** The field takes one line ("Title of a
  new card in Ready to start — Enter opens it, Esc closes"); on `board_written` the pane closes the
  field, opens the new card and starts editing it with the cursor in the issue box. The worker
  still seeds `## Issue` with that line — it is the owner's words, and the card format keeps them
  verbatim — so the editor offers it **selected**: the first keystroke replaces it, and Esc or an
  empty save leaves the card exactly as the field made it. The field no longer stays open for a
  burst of cards; `n` reopens it.

## 5. Referencing cards from the terminal

- **Picker.** In agent or auto mode, `#` at the start or after a space, followed by a character, opens a card picker
  like the `@` file picker (fuzzy on id and title, open cards first); Enter inserts `#K7Q2 `. In terminal mode `#` stays
  a Bash comment. A resolved `#ID` forces the agent route (the `!` prefix still forces terminal).
- **Links in the output.** A `#K7Q2` the agent prints — in a recap, in its prose, or in a
  board-activity line — is a link like a path or a URL is (`src/OutputLinks.*`): hovering
  underlines it and shows `#K7Q2 · <title>`, clicking opens the Switchboard in that tab and the
  card in it, `Ctrl+Shift+L` walks it with the other links, and right-clicking offers the card,
  `#K7Q2` to the clipboard, and `#K7Q2` to the prompt box. Only ids the pane's board knows link;
  an unfiled `#ABCD` and a `#` comment stay text.
- **Attach.** `ask` gains `cards: [{id}]`; the worker prepends a labelled block (front matter, body capped at 16 KiB,
  last 10 thread entries plus summary, file paths), like `attachments`.
- **Post back.** A turn with an attached card appends a thread event with no model call (`↗ discussed in terminal ·
  pane 2 · turn t-41 · "<first prompt line>"`); the agent adds a `board_comment` for decisions, progress or questions.
- **Inline notes.** Every agent board write prints one line in the pane that caused it (`◆ #K7Q2 Voice transcription ·
  moved to Needs QA (LLM) · evidence docs/qa_evidence/…`) plus a toast with Open and Undo. `git pull` changes only badge
  the Board button.
- **Transcripts.** Thread events carry `session/turn` ids; locally they open `TurnTranscript`, elsewhere they read
  "transcript on <user>'s machine". "Export transcript" writes it under `docs/qa_evidence/<date>-<slug>/` and links it.

## 6. Agent autonomy

### 6.1 Tools (pane agents and the board worker, when `board.yaml` exists and autonomy ≠ off)

| Tool | Arguments | Rules enforced by the worker |
|---|---|---|
| `board_list` | `{tab?, status?, labels?, query?, limit≤50}` | one row per card: id, title, status, waiting_on, assignee |
| `board_read` | `{id, thread_entries?≤50}` | front matter, body, thread tail |
| `board_create_card` | `{tab, status, title, request, labels?, source?, related?}` | `request` is the user's words verbatim; fuzzy duplicate check returns `possible_duplicates` unless `not_duplicate_of: [ids]` |
| `board_update_card` | `{id, base_hash, fields?, append_section?: {heading, text}, replace_agent_section?}` | never `id`, `created*`, `source` or owner-authored sections |
| `board_move_card` | `{id, status?, tab?, before?, after?, reason, evidence?}` | into `needs-qa-*` needs `evidence` + `implemented_by`; QA lane → `done` needs a verdict section and a model family different from `implemented_by` |
| `board_comment` | `{id, kind: note\|question\|decision\|evidence\|progress, text}` | appends to the thread; `decision` quotes the owner verbatim |

### 6.2 Policy (system prompt block, versioned in `backend/relay_core/board_policy.md` so evals can pin it)

1. **Capture.** Every distinct request not finished within the turn becomes a card or updates the matching one (search
   first); multi-request prompts split into one card each. Trivial or fully answered asks get no card.
2. **Questions** for the owner go on the card (`kind: question`, numbered, with a recommendation), status `discussing`,
   `waiting_on: owner`; the terminal reply names the card rather than burying questions in scrollback.
3. **Decisions** made in the terminal or a card: a `decision` comment quoting the owner, appended to `## Decisions`.
4. **Work.** Starting: `in-progress`, `assignee: agent`, `implemented_by: <model>`. Landing: `needs-qa-llm` with an
   evidence path and a QA checklist, in the same commit as the change (skill rule).
5. **Unrelated faults** found along the way: a new Bugs inbox card with measured evidence, never a silent fix.
6. **People's cards:** comment only; never reassign.
7. **Labels are the agent's job.** Every work card carries exactly one of `bug` (something built behaves wrongly) or
   `feature` (something new or changed is asked for), chosen from the agent's reading of the request, plus obvious area
   labels. The owner never has to label a card, and the agent does not narrate the labelling.

### 6.3 Guardrails

- No delete tool: closing is `done`/`dropped` with a reason. Owner text (`## Issue`, owner thread entries) is
  hash-recorded and edits to it are refused. Every agent write appends a thread event (actor, model, pane, turn id).
- Limits: 5 creates and 20 other writes per turn, 30 creates per hour per workspace; beyond them `board_rate_limited`
  and the agent summarizes in chat. Writes are atomic and hash-checked like `write_file`.
- Autonomy in `board.yaml` (`off`; `suggest` = writes become proposals accepted in the Board; `auto`) with a per-user
  local override. This repo: `auto` (owner decision); new boards: `suggest`.
- Undo (toast, 30 s) restores the pre-write snapshot; undoing a creation removes the file only if never committed.

### 6.4 QA and iteration

- **Scenarios** (`tests/board_evals/`): fixture repo + scripted owner transcript, run headless via
  `scripts/relay-agent.py` on real providers. ~20 cases: three requests in one prompt; a question answered in-turn (no
  card); a duplicate; an owner decision in chat; implement and land; an unrelated fault; a person's card; a stale hash.
- **Hard assertions** in CI with a stub model: no deletes, owner hashes unchanged, folder matches status, a thread event
  per write, limits and the verdict-on-close contract enforced (owner, 2026-09-20, #76DJ).
- **Judgment metrics** on live models: card precision/recall, verbatim quoting, question quality. The expected-card gold
  set is a `needs-labels` card with a codebook; "good enough to default on" is `needs-review` (skill rules 3–4).
  Results (sample sizes, policy, model ids) go to `docs/qa_evidence/<date>-board-evals/`; this repo dogfoods `auto`.

### 6.5 Cross-provider QA: the signature and the verifier (card #T71W, 2026-09-19)

The QA lane has always asked for a *different* model; it never said which, and it believed whatever
the agent typed into `implemented_by`. Three changes, all in
`backend/relay_core/qa_verifiers.py`:

- **The signature is written, not typed.** The worker knows its own preset and model, so it stamps
  `implemented_by` on entering `in-progress` or a QA lane and `verified_by` on closing out of one,
  as `provider/model` where the provider is the *model's* vendor (`deepseek/deepseek-v4.1-flash`,
  not `openrouter/…`). A Tier A guest pane records the model the harness reports along with the
  harness itself — `anthropic/claude-opus-5-20260514 via claude-code` — because "Claude Code" is
  not a model and next month's Claude Code is a different reviewer. The agent's own argument
  survives only for a guest writing through the bridge, which is the one case the worker cannot know.
- **The card names its verifier.** One ranked table (the owner's order: codex → claude → glm → kimi
  → deepseek → gemini → minimax, then a local endpoint) and one lineage table. The implementer's
  family is skipped; a family in its lineage is moved behind every other lineage but still offered,
  because a second pair of eyes from the same training data is worth less, not nothing; a local
  model is offered last; anything with no CLI on PATH and no stored key is reported as unavailable
  with what is missing. The reasons come from the research report under
  `docs/qa_evidence/2026-09-19-cross-provider-qa/`, and both tables are data with a comment per row.
- **Relay Free never verifies, and is not a family.** Owner, 2026-09-19: *"relay free is never used
  for verifying — so verifying is not available on the free plan."* It is not in the ranking at all;
  it is reported as unavailable with that reason, and a close signed `relay-free/…` is refused. On
  the implementer side it still resolves to the gateway's upstream for the role, so a card written
  on the free plan is verified from outside *that* lineage first rather than by the model behind it.

Availability is always the worker's (PATH, keyring, local endpoints) — the GUI is told, never asked
— and the same function answers `board_read`, `board_card_get` and
`scripts/relay-board.py verifier <ID>`, so a collaborator without the GUI gets the same answer.

## 7. Detecting and converting non-compliant notes

`board_scan` runs on Board open, on watcher changes and on demand (`board.convert`); findings show as a banner ("7
notes can become cards · Review"). Nothing converts without a preview.

| Source | Detection | Conversion |
|---|---|---|
| Issue without front matter | `*.md` in category folders lacking YAML | deterministic header parse (2.2), in place after confirm |
| Folder/status mismatch, duplicate id, merge markers, bad YAML | `check` | fix proposal per field |
| `feature_intake.txt`, `bug_intake.txt` | non-empty paragraphs | one Inbox card per paragraph, text verbatim, `source` quotes it; the model proposes title and tab only |
| `TODO.md`, `NOTES.md`, `IDEAS.md`, checklists (root, `docs/`) | unchecked `- [ ]` items, headed sections | proposed cards; checked items ignored |
| v1 `.relay/scratchpad/*.md` | file exists | items → cards, agent blocks → thread |
| GitHub export (`gh issue list --json`), Trello JSON | JSON shape | cards with `links.github` and labels; comments → thread entries under original authors |

The preview lists proposed cards (title, tab, status, labels, verbatim excerpt) with checkboxes and inline edit;
**Create cards** writes them and leaves originals untouched. A separate explicit **Remove converted text from sources**
deletes converted paragraphs and logs a thread event per card. `TODO:` comments in code are out of scope (noise).

## 8. Relationship to existing habits and documents

- **Intake files.** Quick-add (`n`, `/card`, `relay board add "…"`) replaces them for Relay users; they remain a valid
  capture point without Relay and keep being converted. `issues/README.md` names the Board as intake after Phase 1.
- **Request ledger / memory.** `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` does not exist yet. The Board is the durable,
  shared layer of a request ledger (policy rule 1); a within-turn todo list (Claude Code tasks, opencode `todowrite`)
  still covers steps inside a turn and must not create cards. Reconcile when that research lands.
- **Plans.** `/plan` in a card runs plan mode in the board worker; `plan_written` adds `links.plans` and a thread event;
  Execute runs in a chosen terminal pane with `cards: [{id}]`. `.relay/plans` is not gitignored; see question 4.
- **QA lanes.** Same semantics; the Needs QA column is the lane. QA sweeps use `board_list {status: needs-qa-llm}` and
  `board_move_card`, so the worker, not memory, enforces the verdict contract (any pane may
  close once the verdict is there — owner, 2026-09-20, #76DJ).

## 9. Protocol, components, format

### 9.1 Worker protocol (new section 12 of `AGENT-SESSIONS-PROTOCOL.md`, v1.2)

| Message (GUI → worker) | Event(s) |
|---|---|
| `board_open {workspace}` | `board {rev, config, cards: [{id, path, title, tab, status, labels, assignee, waiting_on, rank, thread_count, last_entry}], problems}` |
| `board_refresh {paths}` (from `QFileSystemWatcher`) | `board_changed {rev, upserts, removed, problems}` |
| `board_card_get {id}` | `board_card {id, hash, front, body, thread: [{entry_id, author, kind, time, text, turn?}]}` |
| `board_create {tab, status, text, author}`, `board_update {id, base_hash, patch}`, `board_move {id, status?, tab?, before?, after?}`, `board_comment {id, text, author}`, `board_undo {write_id}` | `board_changed`; stale hash → `error {code: "board_conflict", id, current_hash}` |
| `board_ask {id, text}` (board worker) | normal turn events with `turn_id` and `card_id`, then `board_changed` |
| `board_scan {sources?}`, `board_convert {finding_ids, edits}`, `board_cleanup_sources {finding_ids}`, `board_check` | `board_scan_result {findings: [{fid, kind, path, range, excerpt, proposal}]}`, `board_converted {cards}`, `board_changed`, `board_problems {items: [{path, code, message, fix?}]}` |

- `ask` gains `cards: [{id}]`; `configure` gains `board: {dir, autonomy, limits}`. Every agent write also emits
  `board_activity {write_id, id, actor, model, action, summary, turn_id}` (inline notes, toasts, Undo).
- `backend/relay_core/board.py` owns parsing, ids, ranks, atomic writes and thread appends (`O_APPEND` + `flock`); the
  GUI never parses Markdown. `scripts/relay-board.py` offers `list`, `add`, `move`, `check`, `index`, `migrate` for
  collaborators without the GUI and for CI.
- **GUI:** `BoardPane`, `BoardModel`, `CardDelegate`, `CardDetailView` (header, body via `FilePreview`/`PlanEditor`,
  `ThreadView` from `TurnTranscript` pieces, reply `RichEditor`), `MovePopup`, composer `CardPicker`, `ConvertDialog`,
  board worker lifecycle in `RelayWindow`, inline activity lines in `Pane`, palette actions, Keymap and hint entries.

### 9.2 File format

`issues/board.yaml` (tab titles default to the capitalized id):

```yaml
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}, {id: design, folder: design},
  {id: marketing, folder: marketing}, {id: planning, folder: planning},
  {id: deferred, filter: "status:deferred"}, {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
```

`issues/features/2026-09-17-voice-transcription.md` (never write `#` unquoted in YAML; ids are stored bare):

```markdown
---
id: K7Q2
status: in-progress
labels: [voice, mvp]
component: [gui, worker]
milestone: desktop-alpha
assignee: agent
implemented_by: Claude Opus 5 (pane 2)
rank: "0i"
created: 2026-09-17T09:12:00-04:00
source: 'issues/feature_intake.txt, 2026-09-17: "add voice transcribe mode (microphone icon). and hold right alt to transcribe. (like warp)"'
acceptance: holding Right Alt records speech and inserts the transcript into the composer
links: {plans: [.relay/plans/2026-09-17-1410-voice-mode.md], commits: [], evidence: [], related: [M3XJ], github: null}
---
# Voice transcription mode (microphone button, hold Right Alt)

## Issue
add voice transcribe mode (microphone icon). and hold right alt to transcribe. (like warp)

## Decisions
- 2026-09-17, owner: "cloud-based, using the existing OpenRouter key; Gemini is fine if cheaper." → default
  `google/gemini-3.5-flash-lite` via OpenRouter (thread entry 20260917T141240Z).

Thread: [threads/K7Q2.md](../threads/K7Q2.md)
```

`issues/threads/K7Q2.md`:

```markdown
<!-- relay:entry 20260917T141203Z-a1 author=owner kind=comment -->
### Elliott Ash · 2026-09-17 10:12
where should transcription run? cheap is fine

<!-- relay:entry 20260917T141240Z-b7 author=agent kind=question model=kimi-k3 turn=s9f2/t-12 -->
### ✦ agent (Kimi K3, board) · 2026-09-17 10:12
1. Local whisper.cpp, or cloud with your OpenRouter key? *(recommend: cloud, gemini-3.5-flash-lite)*
<details><summary>⚙ 2 tools · 8 s</summary>read_file issues/features/…; run_command curl … (exit 0)</details>

<!-- relay:entry 20260917T142010Z-c3 author=agent kind=event -->
- ✦ agent moved Discussing → In progress · assignee agent · pane 2 turn s9f2/t-14
```

## 10. Phased delivery

| Phase | Scope | Effort |
|---|---|---|
| 0. Format | `board.py` parse/write/rank/id/thread append + tests; `relay-board.py check/index/migrate`; migrate 43 issues; README, `.gitattributes`, skill paragraph | M |
| 1. MVP | agent tools + policy v1 + guardrails (M); `BoardPane` columns, tabs, drag/drop, quick add, keys, Ctrl+Shift+S, palette, hints (L); card detail with board worker thread (M); `#` picker, `ask.cards`, post-back, inline notes + Undo (M); protocol doc (S) | L |
| 2. Trust | eval harness + 20 scenarios (M); scan/convert preview for issues and intake (M); filters/labels/search (S); verdict-on-close enforcement, generated VALIDATION list (S); `suggest` proposals UI (S) | M–L |
| 3. Collaboration | front matter merge driver (M); conflict banner (S); TODO/NOTES/GitHub/Trello importers (M); transcript export (S); GitHub Issues two-way sync (L, maybe never) | L |

Dogfood order: Phase 0 plus the agent tools and policy first (usable from terminal panes and by agents without the
Board UI), so autonomy is QA'd before the drag-and-drop pane lands.

## 11. Open questions for the owner

1. **Name:** "Board" in the product with "Switchboard" only as a nickname (recommended), or Switchboard everywhere?
2. **Unify:** OK to migrate the 43 issues to YAML front matter + `threads/`, and add a paragraph to the global
   `issue-tracking` skill? Bugs tab on `changes/` (recommended) or a new `bugs/` folder?
3. **Autonomy edges:** in `auto`, may the agent remove converted intake text itself (today's habit), or does source
   cleanup always need your click (recommended)? Should your quick-adds trigger an automatic triage turn?
4. **Plans and privacy:** commit `.relay/plans` files linked from cards? Do you want private, gitignored cards (a local
   Private tab under `.relay/board-private/`)?
5. **Card agent:** a stateless board worker on the anchor pane's model (recommended), or a cheap triage default
   (`gemini-3.5-flash-lite`) that escalates to the pane's model for real discussion?

## 12. Owner decisions (2026-09-17)

1. **Name:** "Switchboard" everywhere in the product for now (UI, `/switchboard`, docs); revisit during QA. Code and
   tool identifiers may stay short (`board_*`, `BoardPane`) where they are not user-visible.
2. **Unify:** yes. Migrate the 43 issues to YAML front matter + `threads/`, add the paragraph to the global
   `issue-tracking` skill; Bugs tab on `changes/`.
3. **Autonomy edges:** in `auto` the agent may remove or rewrite input text (intake notes, quick-adds, owner-authored
   card text). Every such change is logged in the card's thread as an event holding the original text and the new text,
   so the thread works like a GitHub issue's discussion history and any rewrite can be reverted. This replaces the
   6.3 guardrail that refused edits to hash-recorded owner text; the hash now detects unlogged edits instead.
4. **Plans and privacy:** plans are committed; private cards exist. A card marked private lives outside git (local,
   gitignored) and its plans are private too. Plans are implemented in the Switchboard directly: plan mode writes the
   plan onto a card rather than a free-standing `.relay/plans` file (supersedes the earlier `.relay/plans` decision
   for projects with a Switchboard).
5. **Card agent:** leaning to the pane model; alternatives to be described to the owner before deciding.

### 12.5 Card agent (owner decision, 2026-09-17)

- **Thread replies** come from a dedicated **Switchboard agent** (a worker per board root per window, as in 4.3), whose model is a
  role in Agent options defaulting to the main agent (implemented: `docs/AGENT-SESSIONS-PROTOCOL.md` section 13, role `switchboard`).
- **Pane hand-off (option C):** a card can be pulled into a terminal pane's own conversation (`#K7Q2`, "work on
  #K7Q2"), so the pane agent has the card body, open tasks and thread tail in context and posts progress back.
- **Per-card conversation (option D):** each card keeps a saved conversation so long discussions retain full detail
  (tool results, reasoning). The card file and its append-only thread remain the shared record: a collaborator, or this
  machine after the local state is lost, reconstructs the thread from the file, and an edit to the card invalidates the
  saved conversation (it is reseeded from the file).
- **Chores (option E):** duplicate checks, label and title suggestions and non-compliant-note scans run on the chores
  role, default `google/gemini-3.8-flash` via OpenRouter (Flash agent if no OpenRouter key). They never post thread
  replies and every write is logged.
