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

### 4.2 Board view (`BoardPane`, a `ToolPane` leaf)

Top: tab bar with counts, filter field, label chips, Mine / Agent / Waiting-on-me toggles. Body: horizontally scrolling
columns; a card shows `#ID`, title (2 lines), labels, assignee (✦ = agent), `waiting_on` badge, thread count and an
unread dot (tracked locally in QSettings, not git). Drag between columns (status + folder move) or onto a tab (category
move). Quick add: `+` or `n` opens a growing field; Enter creates the card with the text verbatim as `## Request`.

### 4.3 Card detail

Enter opens it in the right half of the Board (Esc closes; Shift+Enter opens its own pane). Header: title, status and
tab pickers, labels, assignee, links (plans, commits, evidence, related). Body: rendered Markdown; `e` edits
(`PlanEditor`, Ctrl+S, atomic, hash-checked). Thread: entries with author and time; the agent's reply streams;
thinking collapses to "Thought 9 s"; tools collapse to one `turn_summary` line ("⚙ 3 tools · 12 s") expanding via
`turn_transcript_get` (`TurnTranscript`). Reply box: `RichEditor` with composer keys and `/plan`, `/move`, `/assign`.

**Which agent answers:** a dedicated **board worker** per window (an ordinary `worker.py` with its own queue), so card
chats never pollute a pane's conversation. It is stateless between turns: each turn is seeded from the body plus the
thread (older entries summarized past a cap), so the *file* is the memory and a collaborator's Relay continues the same
thread. Its model chip defaults to the anchor pane's preset.

### 4.4 Keyboard and hints

`board.*` Keymap actions, active when the Board has focus: arrows select; Enter/Esc open/close; `n` new; `m` move
(popup, digits pick a column, Tab lists tabs); Alt+Shift+Left/Right move a column, Alt+Shift+Up/Down reorder; `/`
filter (`label:`, `status:`, `@agent`, `waiting:me`, text); `l` labels; `a` assign; `c` reply; `t` insert `#ID` into the
anchor composer and focus it; `y` copy `#ID`; `o` open the file in a preview pane; Ctrl+PgUp/PgDn switch tabs; `?` keys.
**Hints** (WARP.md rule, live Keymap text): palette/button → Ctrl+Shift+S; mouse drag → `m`; clicking `+` → `n`;
"Send to terminal" button → `t`; typed `/board` → the open shortcut.

## 5. Referencing cards from the terminal

- **Picker.** In agent or auto mode, `#` at the start or after a space, followed by a character, opens a card picker
  like the `@` file picker (fuzzy on id and title, open cards first); Enter inserts `#K7Q2 `. In terminal mode `#` stays
  a Bash comment. A resolved `#ID` forces the agent route (the `!` prefix still forces terminal).
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

### 6.3 Guardrails

- No delete tool: closing is `done`/`dropped` with a reason. Owner text (`## Request`, owner thread entries) is
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
  per write, limits and the independence rule enforced.
- **Judgment metrics** on live models: card precision/recall, verbatim quoting, question quality. The expected-card gold
  set is a `needs-labels` card with a codebook; "good enough to default on" is `needs-review` (skill rules 3–4).
  Results (sample sizes, policy, model ids) go to `docs/qa_evidence/<date>-board-evals/`; this repo dogfoods `auto`.

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
  `board_move_card`, so the worker, not memory, enforces independence and the verdict contract.

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

## Request
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
| 2. Trust | eval harness + 20 scenarios (M); scan/convert preview for issues and intake (M); filters/labels/search (S); independence enforcement, generated VALIDATION list (S); `suggest` proposals UI (S) | M–L |
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

- **Thread replies** come from a dedicated **Switchboard agent** (a worker per window, as in 4.3), whose model is a
  role in Agent options defaulting to the main agent (implemented: `docs/AGENT-SESSIONS-PROTOCOL.md` section 13, role `switchboard`).
- **Pane hand-off (option C):** a card can be pulled into a terminal pane's own conversation (`#K7Q2`, "work on
  #K7Q2"), so the pane agent has the card body, open tasks and thread tail in context and posts progress back.
- **Per-card conversation (option D):** each card keeps a saved conversation so long discussions retain full detail
  (tool results, reasoning). The card file and its append-only thread remain the shared record: a collaborator, or this
  machine after the local state is lost, reconstructs the thread from the file, and an edit to the card invalidates the
  saved conversation (it is reseeded from the file).
- **Chores (option E):** duplicate checks, label and title suggestions and non-compliant-note scans run on the chores
  role, default `google/gemini-3.8-flash` via OpenRouter (fast agent if no OpenRouter key). They never post thread
  replies and every write is logged.
