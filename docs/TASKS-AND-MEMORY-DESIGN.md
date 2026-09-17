# Tasks and project memory in the Switchboard: design (proposal, 2026-09-17)

Owner requests (2026-09-17): "we need the task list for sure -- you can compare how warp and claude code do it and give
me a proposal on that, and make it integrated with the switchboard as much as possible (tasks can be whole cards, or
elements on a card, etc)." / "also memories can be viewable / editable in the switchboard alongisde plans and issues."
/ Memory location: "in the repo switchboard, allowing private memories as well like private cards".

Builds on `docs/SWITCHBOARD-DESIGN.md` (sections 2, 4, 6, 8 and owner decisions in 12) and
`docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` (items 2–5 and 9, owner decisions in 9). Nothing here is implemented.
Citation keys follow `docs/AGENT-FEATURES-RESEARCH.md`: **CC:x** = `https://code.claude.com/docs/en/x` (fetched
2026-09-17); **W:x** = `https://docs.warp.dev/x`; **(3P)** =
Claude Code prompt text from the Piebald-AI extraction (`github.com/Piebald-AI/claude-code-system-prompts`), not
published by Anthropic. *Unverified* marks claims not confirmed at a primary source.

## 1. What others do

| | Claude Code | Warp |
|---|---|---|
| Tools | `TaskCreate`, `TaskGet`, `TaskList`, `TaskUpdate` ("status, dependencies, details, or deletes tasks"); `TodoWrite` is legacy (`CLAUDE_CODE_ENABLE_TASKS=0`). Default only on older models; opt in with `CLAUDE_CODE_ENABLE_TODO_TOOLS=1` (CC:tools-reference) | Server-side ops `CreateTodoList`, `UpdatePendingTodos`, `MarkTodosCompleted`; completed items are frozen, the first pending item is the current one |
| Fields | `subject`, `description`, `activeForm` (spinner text), `owner`, `metadata`, `addBlocks`/`addBlockedBy`; statuses pending → in_progress → completed, `deleted` (3P tool-description-taskcreate/taskupdate) | id, title; ● current, ✔ done, ○ not started, ■ cancelled (W:agents/capabilities/task-lists/) |
| Rules | "Mark it as in_progress BEFORE beginning work"; "Never mark a task as completed if: Tests are failing / Implementation is partial"; "Check TaskList first to avoid creating duplicate tasks"; plan mode → "create a task list to track the work" (3P) | Created automatically "for complex requests"; no documented completion rules |
| Display | `Ctrl+T` toggles the checklist in the status area, "up to five tasks at a time"; expanded state restored on resume (CC:interactive-mode) | Chip bottom right, popup list; one chip combines plan and todo list |
| Persistence and sharing | "Tasks persist across context compactions"; `CLAUDE_CODE_TASK_LIST_ID=x` shares a list in `~/.claude/tasks/` across sessions (CC:interactive-mode). Agent teams: lead creates, teammates self-claim "the next unassigned, unblocked task"; claims use file locking; dependents unblock automatically; `TaskCreated`/`TaskCompleted` hooks can veto; "Task status can lag" (CC:agent-teams) | Per conversation; not editable by the user (*unverified*, undocumented) |
| Plans | Plan mode; plan re-read from disk after compaction (CC:context-window) | `/plan` writes a versioned document in Warp Drive's Plans folder; "Any update made by the agent creates a new version"; export as Markdown or check into the repo (W:agents/capabilities/planning/) |
| Memory | CLAUDE.md hierarchy + `CLAUDE.local.md` (gitignored). Auto memory in `~/.claude/projects/<project>/memory/`: `MEMORY.md` index (first 200 lines or 25KB loaded) + one topic file per memory, types user/feedback/project/reference, machine-local, `/memory` to browse; near-limit writes trigger "shorten it" reminders (CC:memory). One fact per file with `name`/`description`/`type` front matter; "treat them as past snapshots to verify"; a shared `team/` store pruned conservatively; "Never write secrets or credentials into a memory" (3P) | Rules: Global (Warp Drive) + `AGENTS.md`/`WARP.md`; applied rules listed under References (W:agent-platform/capabilities/rules/). Suggested rules appear as chips after a response, behind a setting. Agent Memory (preview): facts extracted at conversation end, personal/agent/team stores on Warp servers (W:agents/agent-memory/) |

Others, only where they add an idea: Codex `update_plan` requires ending with every step completed or explicitly
cancelled/deferred, and opencode `todowrite` replaces the whole list and loses it at compaction (both in
`MEMORY-AND-MULTI-REQUEST-RESEARCH.md` section 2). Devin Knowledge: each item has a **trigger description** used for
retrieval, and Devin *suggests* knowledge that the user edits or dismisses (`docs.devin.ai/product-guides/knowledge`).
Windsurf keeps auto memories local and "not committed", and recommends rules/`AGENTS.md` for shared knowledge
(`docs.devin.ai/desktop/cascade/memories`). Cursor removed Memories in 2.1 in favour of Rules (*unverified*: forum and
third-party posts only).

**Lessons.** (1) Keep the model-facing todo tool tiny and familiar; put durability elsewhere. (2) Claude Code's shared
list is a local directory and Warp's is per conversation: neither survives into git, a collaborator or a QA session.
The Switchboard can. (3) Dependencies and claiming matter once several agents share a list. (4) Every product that
auto-writes memory needed caps, an index, and a review path; two retreated to human-owned rules.

## 2. Task model: three layers, one vocabulary

| Layer | Lives in | Lifetime | Written by | Use for |
|---|---|---|---|---|
| **Todo** `T3` | session state (`update_todos`, research item 3) | this session, survives compaction | the pane agent | steps inside a turn or session |
| **Card item** `#K7Q2.a3` | a `## Tasks` checklist in the card body (git, or private) | until done/dropped | owner, any agent, `relay-board`, GitHub's web checkbox | a step of a card's acceptance that lands with the card |
| **Card** `#K7Q2` | `issues/<tab>/…md` + `threads/` | tracker lifetime | owner, agents (policy 6.2) | a request with its own discussion, QA, assignee or landing |

**Item or card?** Make it a card if *any* holds: it needs its own discussion or questions; it gets its own QA verdict or
evidence; a different person or agent owns it; it can land in a separate commit; it is a distinct owner request (policy
rule 1). Otherwise it is an item on the card whose acceptance it serves. A step only this turn cares about is a todo.

### 2.1 Card checklist format

```markdown
## Tasks
- [x] Add OpenRouter transcription call <!-- t:b7 -->
- [ ] Record audio with QAudioSource <!-- t:a3 s=in-progress -->
  - [ ] Handle device permission errors <!-- t:c1 blocked_by=a3 -->
- [ ] #M3XJ Right-Alt push-to-talk <!-- t:d9 card=M3XJ -->
- [x] ~~Local whisper.cpp fallback~~ <!-- t:e2 s=dropped -->
```

- GitHub renders `- [ ]` as a task list and hides the comments; the file stays usable without Relay.
- **Item ids**: 2 Crockford-base32 characters, unique within the card, referenced as `#K7Q2.a3`. Lines without a marker
  (typed by hand or by an agent without Relay) get one on the next Relay write or `relay-board check --fix`.
- **Checkbox is authoritative for open/closed**, the marker refines it. If GitHub's web UI ticks a box whose marker
  says `in-progress`, the item is `done` and the marker is rewritten.
- One nesting level. Deeper structure means the sub-item should be a card (`check` warns).
- **Dependencies** (`blocked_by=a3,#M3XJ`) are stored, never the derived "blocked" state: an item is waiting while a
  blocker is open, and it unblocks automatically when the blocker closes. Explicit `s=blocked` is for external blockers
  and needs a reason in the thread. Cards get `blocked_by: [M3XJ, K7Q2.a3]` in front matter. `check` rejects cycles.

### 2.2 Shared status vocabulary

The model-facing enum stays as it ships in `update_todos` (models know these words from TodoWrite, `todowrite` and
`update_plan`). The worker maps it at the boundary; card markers and the UI use Switchboard words.

| `update_todos` | Item marker | Box | Child card status mirrored into its parent item | Glyph |
|---|---|---|---|---|
| `pending` | (none) `open` | `[ ]` | `inbox`, `discussing`, `ready` | ○ |
| `in_progress` | `in-progress` | `[ ]` | `in-progress` | ◐ |
| `blocked` (reason) | `blocked` | `[ ]` | `needs-review`, `needs-labels`, `needs-ab`, or `waiting_on` set | ⏸ |
| `deferred` (reason) | `deferred` | `[ ]` | `deferred` | ⤼ |
| `completed` | `done` | `[x]` | `needs-qa-llm`, `needs-qa-human` (chip "QA"), `done` | ✓ |
| `cancelled` (reason) | `dropped` | `[x]` + `~~` | `dropped` | ✕ |

An agent may mark an **item** done. Card lane rules are unchanged: into `needs-qa-*` needs evidence, and `done` needs
an independent QA verdict (SWITCHBOARD 6.1).

### 2.3 Extending `update_todos` (additive; nothing in the minimal tool is removed)

The minimal tool as it is being written (`backend/relay_core/todos.py`, uncommitted on 2026-09-17) replaces the whole
list per call, allows only `id, text, status, request_ids, note` (`additionalProperties: false`), and derives ledger
request status from linked todos (`requests.py apply_todos`). Exact changes:

```jsonc
{"items": [{"id": "T3", "text": "Record audio with QAudioSource", "status": "in_progress",
            "request_ids": ["R2"], "note": null,
            "ref": "#K7Q2.a3",          // NEW (model-settable): card or card item this todo works on
            "blocked_by": ["T2"]}]}     // NEW (model-settable): todo ids; the worker rejects cycles
```

- `SPEC` properties and the allowed-field set in `validate()` gain `ref` and `blocked_by`. A resent todo without them
  keeps its earlier values, as `request_ids` already does. `owner` is worker-set only (2.6) and never accepted from
  the model. `RULES` gains one sentence: "When a todo is work on a Switchboard card or card item, set ref."
- Because each call replaces the list, the worker **diffs old vs new status per id** to find transitions.
- The `todos` event items gain `ref`, `ref_title`, `ref_status`, `owner`, `blocked_by`; the result gains
  `ref_errors` and `card_writes`. Stored session state gains the same fields (old sessions load with none).

Worker rules:
1. `ref` must resolve (card exists, item exists or `ref` is a whole card). Otherwise the tool returns
   `ref_errors: [{id, ref, near: ["#K7Q2.a4 …"]}]` and the todo is kept unlinked.
2. **Sync on transition.** When a linked todo changes status, the worker writes the mapped status to the item, batched
   per tool call: one body rewrite + one `kind=task` thread event per changed item, counted as one board write against
   the 6.3 limits. `pending` never writes. With autonomy `suggest`, writes become proposals; with `off`, no sync.
3. A todo whose `ref` is a whole card: `in_progress` sets card `in-progress` and `assignee: agent` if it was `ready`;
   `completed` does **not** move the card and the tool result says
   `"card K7Q2 needs board_move_card to needs-qa-llm with evidence"`.
4. **New item from a todo**: `"ref": "#K7Q2.+"` appends an item with the todo text and returns its new id.
5. **Durable hand-off.** The end-of-turn completion check (research item 5) treats a todo as resolved when it links to
   an item that is still open on a card. The worker sets it to `deferred` with the note "continues on #K7Q2.a3".
   A request ledger entry may carry `ref` the same way, so "Save open requests as cards?" can also offer
   "Add as item on #K7Q2".
6. **Seeding.** When a turn starts from a card (Execute, `/card-work`, or an `ask` with `cards: [{id, work: true}]`),
   the worker builds the todo list from the card's open items without a model call: one todo per item, `ref` set,
   dependencies copied. This is how tasks cross sessions: the card is the shared list (Claude Code's
   `CLAUDE_CODE_TASK_LIST_ID`, but in git).
7. The compaction block (research item 4) prints todos with their refs, plus open items of attached cards (ids and
   text only).

### 2.4 Plans on cards (owner decision 12.4)

- `write_plan` gains `card?` and `steps?: [string]`. On a card it writes `## Plan` (goal, findings, risks,
  verification; prose, no checkboxes) and turns `steps` into `## Tasks` items. Without a Switchboard it writes the file
  as today.
- **Re-planning** matches steps to existing items by id (the model receives ids in the attached card block) or by exact
  text. Done items are kept. Unmatched open items become `dropped` with reason "removed by re-plan", never deleted.
  The previous `## Plan` text goes into the thread event (decision 12.3), which gives Warp-style plan versions for free.
- `plan_written {card, title, items_added, items_dropped}`. **Execute** switches to Build and seeds todos (2.3 rule 6).
  "Execute in fresh context" does the same after `reset`. A private card's plan is private (it is the card).

### 2.5 Promotion and demotion

| Action | Effect (all logged as thread events on both cards) |
|---|---|
| **Promote item → card** (`p` on an item, or op `promote`) | New card in the parent's tab: `parent: K7Q2`, status `ready` (or `in-progress` if the item was), `## Request` = item text verbatim, sub-items become its items. The parent line becomes `- [ ] #M3XJ <title> <!-- t:a3 card=M3XJ -->`, keeping id and position; its state then mirrors the child (table 2.2) |
| **Demote card → item** (`Shift+M` → pick parent, or `board_move_card {into_card}`) | Parent gains an item with the card's title and `was=M3XJ`; the card becomes `dropped` with resolution "merged into #K7Q2.a3" (never deleted, skill rule). Refused if the card is in a QA lane or assigned to a person |
| **Agent suggestions** | The agent proposes promotion (thread `question`) when an item gets 3+ thread comments about it, needs its own evidence, is assigned elsewhere, or stays blocked across 2 sessions |

Parent cards show progress (`▰▰▱ 3/5`) and never auto-close: all children done moves nothing, but the Board shows
"all tasks done · move to Needs QA?".

### 2.6 Subagents, other panes, collaborators

- **Subagents** still do not get `update_todos` (Design C). The parent's `agent` tool gains `todo: "T3"`: the worker
  sets `owner: a2` and `in_progress`. When the subagent finishes, the todo stays `in_progress` with the note
  "a2 finished: <preview> · verify", and the parent marks it done after checking (subagent output is untrusted).
  Subagent ids are session-local and never written to git.
- **Several panes on one card**: an `in-progress` item or card with `assignee` is claimed. Policy: do not start claimed
  work, pick the next open unblocked item (Claude Code teammate workflow). Claims are atomic with `flock` in `board.py`.
- **Git merges.** Card files are not union-merged, and plain three-way merges conflict on *adjacent* checklist edits:
  ticking items 1 and 2 on two branches, or appending two items, both conflict (checked with `git merge-file`
  on 2026-09-17). Therefore:
  1. every item change is also a `kind=task` event in the union-merged thread (`item`, `from`, `to`, `text`, entry id);
  2. a `relay-card` merge driver (`.gitattributes issues/**/*.md merge=relay-card`, registered in local git config
     when the Board opens) merges front matter key by key (SWITCHBOARD 2.1 "later") and `## Tasks` **by item id**:
     union of items, base order with inserts after their predecessor, and for a status conflict the newer thread event
     wins. Other sections fall back to conflict markers;
  3. without the driver, git's text merge applies and `relay-board resolve` rebuilds `## Tasks` from both sides plus
     the thread. The "undefined driver falls back to text merge" behavior is *unverified*; test it.
  - Two people claiming the same item on two branches merge cleanly; `check` flags "claimed twice".

## 3. UI

### 3.1 In the terminal pane (during a turn)

A strip between the composer and the running-agents list (AGENT-FEATURES Design C). It is shown only while todos
exist and printed inline only when the list changes (research item 6 chip folds into it):

```
  ◐ 2/5  Record audio with QAudioSource  #K7Q2.a3                    R 1 open
```
Expanded (at most 6 rows, then "+3 more"):
```
  ✓ T1 Add OpenRouter transcription call     #K7Q2.b7
  ◐ T2 Record audio with QAudioSource        #K7Q2.a3   a2 exploring 0:41
  ○ T3 Handle device permission errors       #K7Q2.c1   waits on T2
  ⤼ T4 Update README                          continues on #K7Q2.f4
```
- **Toggle `agent.tasks` = Ctrl+Shift+K.** Relay's Ctrl+T is New tab (`src/main.cpp:280`), so Claude Code's key is not
  free. Ctrl+Shift+K is unbound in Relay defaults; preset conflicts are *unverified* (check KEYBINDING-PRESETS.md).
  The expanded state persists per pane, like Claude Code's.
- **Down** from the composer's last line focuses the strip, then the agents list, as in Design C. On a row: Enter opens
  the ref'd card on that item in the Switchboard, `t` inserts `#K7Q2.a3` into the composer, `x` cancels the todo
  (asks for a reason), `r` re-asks a request, Esc returns to the composer.
- Linked-item writes print the SWITCHBOARD 5 inline line, merged per turn: `◆ #K7Q2 · 2 tasks done, 1 in progress`.

### 3.2 In the Switchboard

- **Card face**: progress bar and count, next open item on hover or selection, blocked badge (`⏸ waits on #M3XJ`).
- **Card detail**: `## Tasks` is an interactive checklist above the body; Tab moves focus between header, tasks, body
  and thread. **Checklist keys** (active only with checklist focus, so Board keys keep working elsewhere):
  Space done/open · `s` status popup (digits pick, reason prompt for blocked/deferred/dropped) · `n` new item below ·
  `e` or Enter edit text · Tab / Shift+Tab indent / outdent · Alt+Shift+Up/Down reorder (same as card reorder) ·
  `p` promote to card · `t` insert `#K7Q2.a3` into the anchor composer · `y` copy ref · `b` set blocked-by (picker).
- **Tasks tab** (cross-category, like Deferred): one row per open item and per card without items, grouped by card.
  Toggles: *Next* (open and unblocked, default), *Mine*, *Agent*, *Blocked*, *In progress*. Enter opens the item in its
  card; `w` "work on it" sends it to the anchor pane (Execute with seeding); `m` on a card row demotes (`Shift+M`).
- **Plans** render as the `## Plan` section; the thread's plan events offer "Compare with previous" and "Restore".

### 3.3 Shortcut hints (WARP.md rule, live Keymap text)

| Slow path | Hint |
|---|---|
| clicking the task strip | "Next time: Ctrl+Shift+K" |
| clicking a checkbox with the mouse | "Next time: Space" |
| dragging an item | "Next time: Alt+Shift+Up/Down" |
| "Promote to card" from a context menu | "Next time: p" |
| typing `#K7Q2.a3` by hand | "Next time: t on the item" |
| clicking the Memory tab or palette "Memory" | "Next time: /memory" |
| palette "Remember…" | "Next time: /remember <text>" |

## 4. Project memory in the Switchboard

### 4.1 Roles

| Artifact | Holds | Author | Loaded |
|---|---|---|---|
| `WARP.md` / `AGENTS.md` | rules the owner decided | owner (the agent edits only when asked) | always, full (32 KiB cap) |
| **Memory** | durable facts and lessons learned while working ("tests need Xvfb", "OpenRouter lists no reasoning for GLM") | agent or owner | index always, bodies on demand |
| Cards, plans, items | work: requests, decisions about one card, steps | both | when attached |
| Todos, ledger | this session's progress | agent / worker | this session |

Policy text (adapted from 3P "tasks vs memory" and "plan vs memory"): do not save progress (todos), approaches (the
card's plan), requests (cards), anything derivable from code or git, anything already in `WARP.md`, one-off debugging
results, secrets, or personal data about third parties.

### 4.2 Storage and scopes

```text
issues/memory/<name>.md          project shared (git)
issues/memory/LOG.md             append-only history, .gitattributes merge=union
issues/.private/memory/…         project private: same layout, gitignored (issues/.gitignore: .private/)
~/.config/relay/memory/…         user global (optional, see question 6)
```

`issues/.private/` is proposed as the **single private root** for private cards, their threads and plans, and private
memory (`.private/<tab>/`, `.private/threads/`, `.private/memory/`), so the path decision is made once. Caveats: a
worktree does not see it, and `git clean -fdx` deletes it (Relay warns when it finds the folder empty but has local
state saying it was not). Alternatives are in question 1.

```markdown
---
name: gui-tests-need-xvfb
description: GUI checks must run under Xvfb with an isolated XDG_CONFIG_HOME
type: convention            # convention | fact | lesson | reference | preference
paths: ["src/**", "tests/gui/**"]   # optional: bodies auto-attach when matching files are read
pinned: false               # true = body always loaded (counts against the cap)
source: 'owner in pane 2, turn s9f2/t-12: "never run the GUI on my display"'
author: agent (Kimi K3)
created: 2026-09-17
supersedes: []
---
Run GUI verification with `xvfb-run` and `XDG_CONFIG_HOME=$(mktemp -d)`.
**Why:** a live run once overwrote the owner's keybindings. **How to apply:** every GUI QA step.
```

One fact per file (Claude Code's shape) means concurrent additions never conflict and no hand-kept index can drift. The
index is generated at load time from `description`. `name` is the id (a kebab slug, unique per scope; `check` flags
duplicates after a merge). `lesson` requires Why/How. Owner preferences learned by the agent default to private.
`LOG.md` entries mirror card threads: `<!-- relay:entry <id> actor=agent op=update name=… -->` followed by old and
new text.

### 4.3 Agent tools and guardrails

| Tool | Arguments | Worker rules |
|---|---|---|
| `memory_list` | `{scope?, type?, query?}` | name, scope, type, description, pinned |
| `memory_read` | `{name, scope?}` | full file |
| `memory_write` | `{op: create\|update\|archive, scope: shared\|private\|global, name, description, type, body, paths?, pinned?, supersedes?, base_hash?, source, reason?}` | see below |

- **Secrets**: refuse on a regex set (provider key prefixes, `-----BEGIN … PRIVATE KEY`, `password=`, JWTs,
  high-entropy tokens) and on **exact matches of keys in Relay's keystore**, which Relay can check and other tools
  cannot. `relay-board check` (usable as a pre-commit hook) scans cards and memory too.
- **Junk**: body ≤ 1,200 chars, description ≤ 150; fuzzy duplicate check returns `possible_duplicates` unless
  `not_duplicate_of`; `already_in_instructions` when WARP.md/AGENTS.md says the same; limits of 3 writes per turn and
  15 shared creates per day; a **budget** of 60 active memories or 12 KB of index per scope, beyond which writes return
  `memory_full` with the least-used entries, and the agent must merge or archive first.
- **Conflicts with instructions**: a write that contradicts `WARP.md` returns `conflicts_with_instructions` with the
  quote. The agent either drops it or saves with `contradicts: WARP.md`, which shows a banner "Update WARP.md?". Only
  the owner applies that (question 9).
- **No hard delete by agents**: `archive` moves to `memory/archive/`. The owner's **Purge** deletes and warns that git
  history keeps shared content.
- **Logging and undo**: every write appends to `LOG.md`, emits `memory_activity`, prints
  `◆ memory gui-tests-need-xvfb saved (shared) · Undo · Make private` in the pane, and shows a 30 s toast. Any logged
  change can be reverted later from the history view.
- **Autonomy**: `board.yaml` gains `memory: {autonomy: auto|suggest|off}`. `suggest` makes writes proposals (Devin and
  Warp suggested-rules pattern). Agent-written shared memories carry an *unreviewed* dot until the owner opens and keeps
  them (`reviewed: true`).
- **Usage**: `last_used` per memory is tracked locally (state dir, not git). "Tidy" (manual, P3) proposes merges and
  archives of unused memories as one reviewable diff.

### 4.4 Loading into context

After instruction files, labelled
`[Project memory: notes learned in earlier sessions; lower priority than instruction files and the user; verify before relying]`:
1. index lines `- name (type, scope): description`, ordered private > shared > global, then most recently used.
   Cap 200 lines / 8 KB, with the omitted count stated.
2. bodies of `pinned` memories, cap 6 KB;
3. bodies whose `paths` match a file the agent reads, attached then (like path-scoped rules), cap 4 KB per turn.
Re-injected after compaction (research item 4 block). Subagents and the board worker get the index and read-only
tools. `/context` lists what loaded and what was cut. Precedence on conflict: instruction files > private > shared >
global, and a newer `supersedes` wins.

### 4.5 Memory UI in the Switchboard

- A **Memory** tab next to Tasks, a list rather than columns: groups Shared / Private / Global, then type; each row
  shows name, description, unreviewed dot, `paths` chip and last used. The detail pane reuses `CardDetailView`: header
  fields, body (`e` edits with `PlanEditor`, Ctrl+S, hash-checked), and **History** from `LOG.md` with Revert.
- Keys (Memory tab): `n` new · `e` edit · `v` toggle shared/private (moves the file, logged) · `a` archive · `k` keep
  (mark reviewed) · `/` filter (`type:`, `scope:`, `unreviewed`, text) · `w` propose the memory as a WARP.md rule (diff
  preview) · `t` insert `@memory:name` into the composer.
- Card detail shows "Memories from this card" (memories whose `source` cites it).
- Composer: `/remember <text>` saves directly (owner-authored, private unless `--shared`); `/memory` opens the tab.
- Conversion (SWITCHBOARD 7): the scan also offers to import `CLAUDE.local.md` notes and
  `~/.claude/projects/<project>/memory/*.md` into private memory, with a preview. Originals are untouched.

## 5. Protocol sketch (additive to `AGENT-SESSIONS-PROTOCOL.md` section 12)

```
← todos {turn_id, items:[{id, text, status, request_ids, note, ref?, ref_title?, ref_status?, owner?, blocked_by?}], card_writes?}
→ ask {…, cards:[{id, work?: true}]}                   (work = seed todos from open items)
← board_card {…, parent?, children:[id], tasks:[{item, text, status, depth, card?, blocked_by, waiting}], progress:{done, total}}
→ board_tasks {filter}                                 ← board_task_rows {rows:[{card, item?, text, status, assignee, waiting}]}
→ board_task_ops {card, base_hash, ops:[{op: add|set|edit|move|drop|promote, item?, text?, status?, reason?, after?, depth?, blocked_by?}], author}
                                                       ← board_changed {…, created?:[id]} | error {code:"board_conflict"}
→ board_move {id, into_card?, …}                       (demote)
tool board_update_tasks {id, base_hash?, ops:[…same…]} (agents; limits 6.3; each op → thread kind=task event)
tool write_plan {title, content, card?, steps?}        ← plan_written {card?, path?, title, items_added, items_dropped}
tool agent {…, todo?: "T3"}
→ memory_open {workspace}  ← memory {rev, scopes:[{scope, dir, writable}], items:[{name, scope, type, description, pinned, paths, status, reviewed, hash}], budget:{index_bytes, cap}, problems}
→ memory_get {scope, name} ← memory_item {…, body, history:[{entry_id, actor, op, time, old, new}]}
→ memory_save {scope, name, base_hash, fields, body} | memory_move {name, from, to} | memory_archive {…} | memory_revert {entry_id} | memory_undo {write_id}
← memory_changed {rev, upserts, removed} · memory_activity {write_id, actor, model, op, scope, name, summary, turn_id}
→ configure {…, memory: {enabled, autonomy, caps}}
```

Components: `board.py` gains the checklist parser and writer (byte-preserving outside `## Tasks`), item ids, and the
merge driver entry point; new `backend/relay_core/memory.py` (parse, index, caps, secret scan, log); `requests.py`
ledger `ref`; GUI `TaskStrip`, `ChecklistView`, `TasksTab`, `MemoryTab`, Keymap and hint entries.

## 6. Phases

| Phase | Scope | Effort |
|---|---|---|
| T0 | `ref`/`owner`/`blocked_by` on `update_todos` (ref validation, no sync yet), status map, `todos` event fields, task strip + Ctrl+Shift+K + hints (the minimal tool ships first) | S |
| T1 | `## Tasks` format in `board.py` (ids, markers, GitHub-toggle tolerance), `board_update_tasks`, todo→item sync, seeding, completion-check hand-off, `write_plan {card, steps}`, card-face progress, checklist keys | M |
| M1 | memory format, 3 tools, loading/caps, `LOG.md`, undo, secret scan, policy, `.private/` root; Memory tab | M |
| T2 | Tasks tab, promote/demote, parent/child mirroring, dependencies + cycle check, `agent {todo}` | M |
| T3 | `relay-card` merge driver (front matter + tasks by id), `relay-board resolve` | M |
| M2 | WARP.md conflict/promotion, `/remember`, imports from Claude Code memory, global scope, Tidy proposals | S–M |

Order: T0 → T1 → M1 → T2 → T3 → M2. T3 must land before subagent worktrees (Design C P2) or real collaborators.

## 7. Tests and evals

- **Unit** (`tests/test_board_tasks.py`, `tests/test_memory.py`): checklist round-trip leaves the rest of the file
  byte-identical; unmarked and GitHub-toggled lines; id collisions; re-plan keeps done items and drops the rest;
  promote/demote event pairs; cycle rejection; sync writes exactly one event per transition and none for `pending`;
  `ref` errors; seeding from items; completion check accepts hand-off. Merge driver fixtures: adjacent toggles,
  concurrent appends, same-item status race (newer event wins), double claim flagged. Memory: caps and `memory_full`,
  duplicate and instruction checks, the secret corpus (true positives plus look-alikes such as git SHAs and UUIDs),
  keystore exact match, load order and truncation notice, revert from log, private files never in `git ls-files`.
- **Scenario evals** (extend `tests/board_evals/` and `scripts/eval-requests.py`, stub + live presets, 3 runs each):
  1. three asks, two belonging to an attached card: items added there, one new card;
  2. plan on a card, stop, new session Execute: todos seeded, finished items not redone;
  3. subagent given `todo`: parent verifies before completing;
  4. ask to split a large item: promotion with verbatim text;
  5. two panes on one card: no double claim;
  6. memory precision/recall on a labelled corpus of 40 turns (should-save facts vs progress notes, derivable facts,
     secrets, WARP.md duplicates); gold set as a `needs-labels` card;
  7. recall: a convention saved in session 1 is followed in session 5 after two compactions;
  8. a 30-session growth simulation: active count stays under budget, zero secrets written.
- **GUI QA under Xvfb**: strip counts match `todos` events; Space on an item writes the marker and the thread event;
  Memory tab edit + revert; hints fire once per slow path.

## 8. Open questions for the owner

1. **Private root**: `issues/.private/` gitignored, shared by private cards, plans and memory (recommended: in-repo,
   visible to editors and agents), or `$XDG_DATA_HOME/relay/private/<repo>/` (survives `git clean` and is shared across
   worktrees, but hidden)?
2. **Task toggle key**: Ctrl+Shift+K (recommended; Ctrl+T is New tab in Relay), or no global key and only Down-focus?
3. **Status words**: keep the model-facing `update_todos` enum and map to Switchboard words (recommended), or switch
   the tool to `open/in-progress/done/dropped`?
4. **Sync timing**: write linked items on every transition, batched per call (recommended), or once at end of turn
   (fewer writes, but a crash loses progress)?
5. **Item vs card threshold**: are the section 2 rules right? In particular, should every separately *landable* step
   be a card (recommended), even if small?
6. **Global memory** (`~/.config/relay/memory/`, cross-project personal preferences): include, small and
   private-by-nature (recommended), or project scopes only?
7. **Memory autonomy here**: `auto` for private and shared with an unreviewed dot (recommended), or `suggest` for
   shared?
8. **Default scope** of agent-learned owner preferences: private (recommended) or shared?
9. **WARP.md**: memory can only *propose* a WARP.md change for your click (recommended), or may the agent apply it in
   `auto`, logged like card rewrites (decision 12.3)?
10. **Memory identity**: slug names (recommended, readable in `@memory:name`) or 4-character card-style ids?
11. **Imports**: offer to import Claude Code auto memory and `CLAUDE.local.md` into private memory (recommended, with
    preview)?
