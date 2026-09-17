# Scratchpad: research and design (proposal, 2026-09-17)

Owner request (2026-09-17): "a scratchpad button that allows me to start a kind of discussion checklist,
operating a little bit like we are doing this feature intake — a dynamic user-agent comms pad. Research
similar features and propose a design. Generally per project." Nothing here is implemented.
Citation keys follow `docs/AGENT-FEATURES-RESEARCH.md` (**CC:x**, **W:x**, **OC:x**, **CX:x**).
*Unverified (search summary)* marks claims seen only in search-result summaries, not on the page itself.

## 1. The habit being productized

Today (`issues/feature_intake.txt`, `issues/README.md`, the `issue-tracking` skill):

1. The owner jots rough, blank-line-separated ideas in `feature_intake.txt` / `bug_intake.txt`.
2. The owner asks the agent to review. The agent replies in chat with recommendations and numbered questions.
3. The owner answers in chat, tersely: "1 yes, 2 coexist, 3 later".
4. The agent files issues (the Source field quotes the intake text word for word), records owner decisions in the
   issue, and removes the processed lines from intake.

What goes wrong: the questions and answers live in chat scrollback, not beside the items. Items that are "to discuss"
stay in intake with no record of the discussion. Nothing shows which items are waiting on the owner and which on
the agent. Compaction or `/new` loses the thread.

## 2. Research: shared user-agent documents

| Product | Shared artifact | Agent asks / user answers | Status signal | Agent edits: visibility, undo, conflicts |
|---|---|---|---|---|
| Claude Code | Task list (`TaskCreate/Update/List`, Ctrl+T toggles), plan file editable with Ctrl+G, CLAUDE.md and auto memory `MEMORY.md` browsed with `/memory` (CC:interactive-mode; CC:permission-modes; https://code.claude.com/docs/en/memory) | `AskUserQuestion`: multiple choice or multi-select, a recommendation and an explanation under each option, keyboard navigation (https://code.claude.com/docs/en/agent-sdk/user-input) *unverified (search summary)* | Task status set by the agent only | Memory files are plain Markdown the user can edit. "Saved 2 memories" notice. No diff of memory writes |
| Cursor | Plan Mode: asks clarifying questions, then writes a Markdown plan whose to-dos you can edit. "Save to workspace" moves it to `.cursor/plans/` (https://cursor.com/docs/agent/plan-mode; https://cursor.com/blog/plan-mode) | Questions come before the plan. Answers go in chat | Editable to-dos | Plans live in the home dir until saved. Notepads and background-agent todos *unverified* |
| Warp | Plans in Drive, a rich editor, "any update made by the agent creates a new version", compare and restore, `@plans` reference (https://docs.warp.dev/agents/capabilities/planning/). Task lists ● ✔ ○ ■, agent-owned (https://docs.warp.dev/agents/capabilities/task-lists/). Notebooks: Markdown plus runnable command blocks (https://docs.warp.dev/knowledge-and-collaboration/warp-drive/notebooks/) | In chat | Task glyphs. The user cannot edit tasks | **Version per agent update** is the key pattern. If the user edits the plan mid-run, the agent adjusts once told |
| Kiro | Specs: `requirements.md`, `design.md`, `tasks.md` in the repo. Tasks show real-time status, can run singly or in dependency "waves" (https://kiro.dev/docs/specs/) | Iterates phase by phase | Checkbox tasks. Per-task "Start task" affordance *unverified* | Files in the repo, so git is the history |
| GitHub Copilot Workspace | Spec with "current state" and "proposed state" bullet lists, then a plan. Both editable. Editing regenerates everything downstream (https://githubnext.com/projects/copilot-workspace/; https://github.com/githubnext/copilot-workspace-user-manual) *unverified (search summary)* | Edits act as answers | — | Regenerates downstream rather than merging |
| Codex CLI | `update_plan{explanation, plan[{step,status}]}`, one step in progress (CX:core/src/tools/handlers/plan_spec.rs). `/plan` then "Implement this plan?" | Plan mode questions | pending / in_progress / completed | Agent-owned |
| opencode | `todowrite` tool, inline list plus sidebar (OC:oc/tool/todo.ts) | — | Todo status | Agent-owned |
| Aider | `/ask` to discuss, `/code` to change, `/architect` for proposer and editor models. Recommended: alternate ask and code (https://aider.chat/docs/usage/modes.html) | In chat | — | Git commit per edit |
| JetBrains Junie | Ask mode brainstorms and plans without edits. CLI plan mode (Shift+Tab, `/plan`) writes a design doc (https://junie.jetbrains.com/docs/junie-cli.html) *unverified (search summary)* | In chat | Plan checklist | — |
| Replit Agent | Plan mode builds an ordered task list you refine, then approve. Checkpoints and rollback (https://docs.replit.com/core-concepts/agent) *unverified (search summary)* | Brainstorm in chat | Task list | Rollback to checkpoints |
| Devin | Planner. Knowledge suggestions drafted from chat feedback, which you edit, save, dismiss or regenerate (https://docs.devin.ai/product-guides/knowledge) *unverified (search summary)* | — | — | **Agent proposes, user accepts** |
| Zed | Text threads: the whole conversation is an editable text buffer (https://zed.dev/docs/ai/text-threads). The Rules Library was replaced by skills and AGENTS.md *unverified (search summary)* | Inline in the buffer | — | The user can rewrite history |
| Linear agents | @mention or delegation starts an agent session with states pending / active / awaitingInput / error / complete / stale. Agents should read frozen "Agent Activities", not editable comments (https://linear.app/developers/agent-interaction; https://linear.app/developers/agent-best-practices) *unverified (search summary)* | Elicitation activity becomes a comment | Session state visible to the user | **Snapshot what the user said**, because comments change |
| Notion AI | @mention an agent in a page comment. An "eyes" indicator acknowledges it, with a notification on reply. Agents create inline comments (https://www.notion.com/help/notion-agent) *unverified (search summary)* | Comment threads | — | Notification on response |
| Google Docs + Gemini | Summarizes open threads, drafts replies, suggests edits for review. "Doesn't automatically post … or make any changes on its own" (https://support.google.com/docs/answer/17133843) *unverified (search summary)* | Threads anchored to text | Resolved / open threads | **Suggestions, not edits** |
| Obsidian (Smart Connections, Copilot) | Chat threads attached to a note, Quick Ask on a selection with insert or replace (https://smartconnections.app/obsidian-copilot/) *unverified (search summary)* | Beside the note | — | — |
| ADR tools (log4brains, MADR) | Dated Markdown decisions in the repo. Status proposed / accepted / deprecated / superseded, plus draft (https://github.com/thomvaill/log4brains) | — | Status line | Git |

**Patterns worth taking:** (a) split ownership: the owner's text is never rewritten, the agent writes only its own
blocks and anything larger is a suggestion (Gemini, Devin); (b) a snapshot per agent update, with diff and restore
(Warp plans); (c) freeze what the user said at review time and review only what changed (Linear); (d) numbered
questions with a recommended answer (AskUserQuestion); (e) visible waiting states (Linear, task glyphs);
(f) checkboxes as approval (Kiro, Cursor to-dos); (g) a notification when the agent replies (Notion);
(h) plain Markdown in the repo (Kiro, ADRs, `.cursor/plans`).

No product found combines per-item threaded Q&A, terse numbered answers and turning items into issues in one editable
file. That combination is the gap Relay's scratchpad fills.

## 3. Design

### 3.1 Files

- `<workspace>/.relay/scratchpad/main.md` is the default pad. `/scratch new <name>` creates `<name>.md` for a topic.
- `<workspace>/.relay/scratchpad/archive/YYYY-MM.md` holds resolved items, moved there verbatim.
- `<workspace>/.relay/scratchpad/.state/` is Relay's bookkeeping: `state.json` (per item, the owner-text hash at the
  last review, review id, open question numbers) and `history/<review_id>.md` (the pad before each agent merge, for
  diff and restore; last 50 kept).
- `configure` gains `scratch_dir` (default above). The pad is per project, like `plans_dir`.

### 3.2 Format: plain Markdown with a light, forgiving structure

```markdown
# Scratchpad

## Inbox
dont hide the prompt box when any program is running ...

voice transcribe mode (mic icon), hold right alt

## Items
### S7 · Scratchpad button · `asking`
to discuss: add a scratchpad button that allow me to start a kind of discussion checklist ...

> **agent** · r3 · 2026-09-17 14:02
> Recommend a per-project Markdown pad in `.relay/scratchpad/` edited in a pane beside the terminal.
> 1. Replace feature_intake.txt, or coexist? *(recommend: coexist, with import)*
> 2. Commit the pad to git? *(recommend: no)*

1 coexist for now
2 yes, commit it

- [ ] file feature issue: "Scratchpad pane and review loop"
- [ ] plan
- [ ] queue: "prototype scratch_merge parser"

### S8 · Voice transcribe · `filed` → issues/features/2026-09-17-voice-transcribe.md
```

The parser rules are few, and everything else is kept as free text:

| Element | Rule | Owner |
|---|---|---|
| Inbox entry | a paragraph under `## Inbox`. The agent turns each one into an item and moves the owner's text into it word for word | owner |
| Item | `### S<n> · <title> · \`<status>\`` (optionally `→ <link>`). Ids are unique within the project and never reused | agent assigns the id. The title can be edited by both |
| Owner text | any lines in the item that are not an agent block, an answer line or an action line | owner |
| Agent block | a blockquote starting `> **agent** · r<round>` | agent only |
| Questions | numbered lines inside an agent block, with an optional `*(recommend: …)*` | agent |
| Answers | lines `<n> <text>` (also `<n>.`, `<n>:`, `<n>)`) after that block. They refer to that block's questions | owner |
| Actions | `- [ ] <verb>: <detail>`, where verb is `file feature issue`, `file change issue`, `queue`, `plan`, `archive`, `drop`. Ticking one approves it | agent proposes, owner ticks. The owner may also add one |

Statuses (pattern e):

| Status | Meaning |
|---|---|
| `new` | not reviewed yet |
| `asking` | waiting on the owner |
| `answered` | computed when answers or owner text changed since the last review: waiting on the agent |
| `ready` | the agent has enough and proposes actions |
| `queued` / `planned` | the linked turn or plan is running |
| `filed` / `done` / `dropped` | resolved, eligible for archive |

Owner edits to a status are respected on the next review.
Private blocks (`<!-- relay:private -->` … `<!-- /relay:private -->`) are never sent to the provider.

### 3.3 Opening it

- **Buttons.** A Scratchpad button (a notepad glyph) in the tab-bar button row that the "new tab / new pane buttons"
  intake item adds. A palette action `scratch.open` ("Scratchpad", aliases: notes, intake, discuss, checklist, todo).
- **Shortcut.** `Ctrl+Shift+J` ("jot") in the Relay preset, mapped in each preset in `docs/KEYBINDING-PRESETS.md`.
  Add a shortcut-hint registry entry for the button and palette paths (standing rule in `WARP.md`).
- **Slash command.** `/scratch` opens the pad; `/scratch <text>` appends to the Inbox without opening it (fast
  capture); `/scratch review`, `/scratch import`, `/scratch new <name>`; `/scratch 1 yes, 2 …` answers (section 3.5).
- **Pane.** A `ScratchpadPane` tool pane opens beside the anchor terminal. It wraps `relay::PlanEditor`
  (`src/FilePanes.h`), with `setPlanActions(false)` and a scratchpad bar in its place: **Review** (Ctrl+Enter) with a
  reviewer chip (pane and model), **Import intake**, **Archive resolved**, **Changes**, **Mark seen**, a one-line
  **reply field**, and status counts (`3 asking · 2 ready · 1 answered`) that jump to the next such item.
- One pad per project per window. Opening it again focuses the existing pane.

### 3.4 The review loop

1. **Trigger.** Review runs on request: the button, Ctrl+Enter in the pad, or `/scratch review`. "Review on save" is a
   setting, off by default (BYOK cost, privacy). While it is on, a save 20 s after the last change triggers a review,
   and only when some item is `new` or `answered`.
2. **Reviewer.** The agent of the pad's anchor pane, chosen in the header chip. It runs a normal turn with a `turn_id`,
   so thinking, tool summaries and Stop all work. The turn uses the plan-mode tool filter: read_file, list_directory,
   run_command, skills, plus `scratch_update`. It has no write_file. It can read `issues/`, docs and code to answer well.
3. **Input.** Only the changed items go in, in full: Inbox entries plus items whose owner-text hash differs from
   `state.json`. Answer lines and ticked actions count as changes. Every other item goes in as a one-line index
   (id, title, status) so the agent can spot duplicates. Archive and private blocks are excluded.
   The base text is frozen per review (pattern c).
4. **Output.** The agent calls `scratch_update` with structured edits only:
   `{new_items: [{from_inbox, title, status}], items: [{id, status, title?, note, questions: [{text, recommend?}], actions: [{verb, detail}], link?}], summary}`.
   It cannot rewrite owner text (pattern a). A merge suggestion ("S3 duplicates S9") is itself an action the owner ticks.
5. **Merge.** Relay turns the edits into text against the live buffer (section 3.6). Questions are numbered 1…n per
   round, across all items, to match the "1 yes, 2 …" habit. Open questions from earlier rounds are renumbered into the
   new round and marked `(from r2)`.
6. **Notify** (pattern g): a pad tab badge, an inline terminal line `✦ scratchpad r3: 4 items · 5 questions · 2 ready`,
   and a desktop notification if the window is unfocused (the recap "away" rule). The summary is the turn's answer.

### 3.5 Answers

- **Inline.** Type `1 yes` under the agent block, or tick an action. The item becomes `answered`.
- **Quick reply.** In the pad's reply field or the composer, `/scratch 1 yes, 2 coexist, 5 later`. The worker splits
  the reply on `,`/`;` or newlines before a number and places each answer under its question in the latest round.
  This makes no model call. Unknown numbers are reported and nothing else changes.
- **Composer shortcut.** When the pane's last agent turn was a scratch review and the prompt starts with a number and
  answer pattern, a chip "Answer scratchpad r3 (Tab)" appears, so the owner's chat habit keeps working.
- **"Answer and review"** (Ctrl+Enter in the reply field) places the answers, then starts the next round at once.

### 3.6 Change visibility, undo and concurrency

- **One merge path, in Python.** The GUI sends the current buffer and its revision. The worker applies the pending
  edits by item id and returns the new text, the changed ranges and any conflicts. The GUI replaces the document
  inside one `QTextCursor` edit block, so **Ctrl+Z undoes the whole agent update**, then saves atomically
  (`QSaveFile`, as `PlanEditor::save` does).
- **Typing during a review never blocks.** If the revision changed before the merge applies, the GUI resends the merge
  with the newer text. That is cheap and makes no model call.
- **Conflicts** (the owner deleted the item, edited an agent block, or changed the status since the base snapshot)
  are held: a banner in the item offers Apply / Discard / Show. Held edits are never forced.
- **Highlights.** Changed ranges get a left-gutter bar and a faint background (`extraSelections`). They stay until
  **Mark seen**, or until the owner edits the item.
- **Changes.** A unified diff of `history/<review_id>.md` against now, rendered like write_file diffs.
- **Restore round.** Restores that snapshot and warns if owner text changed since.
- **External writers.** A `QFileSystemWatcher` (as for keybindings, `ARCHITECTURE.md` section 4) catches an editor,
  `git pull` or another window: a clean buffer reloads; a dirty one is 3-way merged by item id with disk as "theirs",
  collisions going to the conflict banner. One review runs per pad; a second request queues.

### 3.7 Actions

| Action | What Relay does | Result in the pad |
|---|---|---|
| file feature/change issue | Queues an agent turn (build mode, `issue-tracking` skill loaded). The prompt holds the item's owner text word for word, the Q&A as "Owner decisions", and the repo conventions pointer (`issues/README.md`). The agent writes the issue and calls `scratch_update {status: filed, link}` | `filed → issues/...md`. The issue's Source quotes the owner text, as today |
| queue | Adds a cyan ✦ prompt to the pane's combined queue with the item context | `queued`, then `done` when the turn ends, with a turn link |
| plan | `set_mode plan` plus an ask. `plan_written` opens the plan pane | `planned → .relay/plans/...` |
| archive / drop | Moves the item word for word to `archive/YYYY-MM.md` | removed from the pad |
| File all ready | One turn filing every ticked file action, like today's batch triage | one status per item |

Ticking a checkbox is the owner saying "do this". It is not a per-tool approval, so it fits the decision
"no per-action tool approvals". Actions run through the existing queue, so ordering, Stop and Resume behave as they do
for any prompt.

### 3.8 Relation to `feature_intake.txt` / `bug_intake.txt`

Recommended: **coexist, with import**. **Import intake** (button or `/scratch import`) moves each paragraph word for
word into the Inbox, tagged `feature` or `bug`, and removes it from the `.txt` only after the pad is saved (git diff
shows both sides). The intake files stay valid for agents without Relay and for the global skill. Once the pad has
proven itself, `issues/README.md` can name `.relay/scratchpad/main.md` as this repo's intake (open decision 1).

### 3.9 Privacy

Nothing leaves the machine until a review or action runs; review on save is off by default. **Sent:** changed items
in full plus the one-line index, the owner's answers, whatever the agent reads with its tools, and the pane's usual
instructions and conversation (a review is a turn in it). **Never sent:** private blocks, the archive, `.state/`.
The bar shows "sends: 3 items · ~1.2k tokens" before Review; README "What goes to your provider" gains one line.

## 4. Protocol additions (section 12 of `AGENT-SESSIONS-PROTOCOL.md`, v1.2)

| Message (GUI → worker) | Event(s) |
|---|---|
| `scratch_parse {path, text}` (debounced 300 ms, local) | `scratch_items {path, rev, items: [{id, title, status, open_questions, range: [start, end], changed}], inbox: n}` |
| `scratch_review {path, text, rev, scope: "changed"\|"all"\|[ids]}` | normal turn events with `turn_id`, then `scratch_proposal {path, review_id, base_rev, round, questions: n, summary}` |
| `scratch_merge {path, review_id, text, rev}` | `scratch_merged {path, review_id, rev, text, changed: [{id, start, end}], conflicts: [{id, reason}]}` |
| `scratch_answer {path, text, rev, reply}` | `scratch_merged` (no model call), or `error {code: "scratch_unknown_question", numbers}` |
| `scratch_action {path, item, action, text}` | `scratch_action_prompt {item, action, mode: "build"\|"plan", prompt, skills: [...]}`. The GUI then uses the existing queue, `set_mode` and `ask` |
| `scratch_import {path, text, rev, sources: [abs paths]}` | `scratch_merged` plus `scratch_imported {moved: [{source, count}]}`. The GUI rewrites the `.txt` files after saving |
| `scratch_restore {path, review_id}` | `scratch_merged` |

- Model-facing tool `scratch_update` (section 3.4, step 4) exists only in turns started by `scratch_review` or
  `scratch_action`. A call from an action turn produces `scratch_proposal` too, and the GUI merges it the same way.
- `configure` gains `scratch_dir` and `scratch_review_on_save`.
- The parser and merge live in `backend/relay_core/scratchpad.py` with unit tests. The GUI never parses Markdown itself.

## 5. GUI pieces and effort

| Piece | Effort |
|---|---|
| `scratchpad.py`: parser, id allocation, merge, 3-way external merge, answer placement, import, archive, plus tests | M |
| Review turn: changed-item selection, `scratch_update` tool, plan-mode filter, prompt | M |
| `ScratchpadPane` (PlanEditor + bar, reviewer chip, outline counts, reply field), pane restore | M |
| Open paths: button, `scratch.open` palette action with aliases, `Ctrl+Shift+J` in 4 presets, hint entries | S |
| `/scratch` slash forms and the composer "Answer scratchpad" chip | S |
| Merge application as one undo block, change highlights, Mark seen, Changes diff, Restore round, conflict banners | M |
| Actions: checkbox detection, action prompts through the queue, status follow-up on turn end, File all ready | S–M |
| Notifications: tab badge, inline `✦ scratchpad` line, desktop notification when away | S |
| Intake import and `.txt` rewrite | S |
| Docs: protocol v1.2, ARCHITECTURE file panes section, README feature tour and privacy line | S |

Total: **L** (about 4 M plus 5 S). First slice (**M**): the pad file and pane, manual Review with `scratch_update`,
merge as one undo block with highlights, quick answers, and the "file issue" action. Import, notifications, restore
and external 3-way merge follow.

## 6. Open decisions for the owner

1. **Intake.** (a) Coexist with import (recommended). (b) Replace `feature_intake.txt` / `bug_intake.txt` for this repo
   now, updating `issues/README.md`. (c) Keep them separate: the pad is for discussion only and intake stays the
   capture point.
2. **Git.** Commit `.relay/scratchpad/*.md` (the discussion becomes project history, like ADRs, and other agents can
   read it) or gitignore it (private brainstorming). Recommended: commit `main.md` and the archive, gitignore `.state/`.
   Private blocks stay in the file either way, so gitignore the whole pad if they matter.
3. **Autonomy of actions.** Must the owner tick every action (recommended to start)? Or may the agent file an issue on
   its own when every question on the item is answered and it recommended filing? The second matches today's
   "agent files issues and clears intake" but spends tokens and writes to `issues/` without a tick.
