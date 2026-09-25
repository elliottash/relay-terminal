# Memory and multiple requests: research and recommendations (2026-09-17)

Owner request (2026-09-17): "research how claude code and opencode maintain memory and deal with multiple requests,
to make sure things dont get missed when i type multiple requests, and across long convos, see if we need anything
there. you can check warp as well, but my sense is, claude code / codex are much better at that than warp."

Nothing here is implemented. Sources: Codex (`openai/codex@main`) and opencode (`anomalyco/opencode@dev`, formerly
`sst/opencode`) were read as source on 2026-09-17; Claude Code and Warp from official docs. Claude Code's internal prompt
text is not published by Anthropic: quotes marked **(3P)** come from the Piebald-AI extraction
(https://github.com/Piebald-AI/claude-code-system-prompts, ccVersion 2.1.271). **UNVERIFIED** marks claims not confirmed
at the source. `main`/`dev` links move. Citation keys from `docs/AGENT-FEATURES-RESEARCH.md` still apply.

## 0. Summary

- The others do not rely on one mechanism. They stack four: **(a)** a visible todo/plan list the model maintains,
  **(b)** prompt rules like "keep going until resolved, finish with every item completed or explicitly cancelled",
  **(c)** mid-turn delivery of typed messages at tool boundaries, and **(d)** compaction that carries user messages
  verbatim and re-injects durable state (instructions, plan, todos, recent files).
- Claude Code is the most complete on (d). Codex is the strictest on (b) and carries ~20K tokens of user messages
  verbatim. opencode has (a) and (c) but does not feed todos back after compaction, and users report exactly that.
  Warp documents only turn-level queueing and an unpublished summary, with a reported "rules ignored after
  summarization" bug. The owner's sense holds.
- Relay already has (c) (steer + requeue) and a structured summary, but has **no (a) or (b)**. It also has several
  concrete ways to lose a request (section 1): a 12-step turn limit, cancel/interrupt removing the prompt from history,
  unframed steers that drop attachments, and compaction that can summarize away the current turn's original prompt.

## 1. Relay today: where a request can get lost

Read from code (`R:` = repository path).

| # | Behavior | Evidence | Risk |
|---|---|---|---|
| G1 | A turn ends at **12 model steps or 24 tool calls** with `error` "Stopped at the model-step limit", and an error **pauses the queue** | `R:backend/relay_core/agent.py:75,361,400,427`; `queue.py:_dispatch` | A message with 3–5 asks easily needs more than 12 steps. The remaining asks never start, and later queued prompts wait for Resume |
| G2 | Cancel, interrupt and failure truncate `messages` to **before the user prompt**, then append "The previous turn was cancelled…" | `agent.py:429-440`; `_turn_start` = index of the user message | The interrupted request text, plus any steer delivered in that turn, is gone from the model's history (the GUI still shows it). After Ctrl+Alt+Enter the model does not know what it was doing |
| G3 | Steers are appended as a bare `user` message, joined with blank lines. `take_steer` returns only the prompt strings | `queue.py:173`; `agent.py:372-377` | No "sent while you were working" framing and no rule to keep the original task. **Steer attachments and context are discarded** |
| G4 | A steer the turn never reached comes back as `steer_returned` and the GUI puts it first in the queue | `queue.py:201`; `R:src/main.cpp:1485-1500`; `tests/test_queue.py:350-397` | Good: not dropped |
| G5 | `turn_starts` counts **every** `user` message as a turn start (steers, subagent notes, cancel notes, the summary itself). Compaction keeps the last 2 | `context.py:25,122` | Mid-turn auto-compaction after two steers can summarize away the turn's original prompt |
| G6 | Summary sections are Objective / Decisions and constraints / Files touched / Commands / Open items / Next step. User *facts* are kept verbatim, *requests* are not | `context.py:32` | Several asks collapse into one "Objective". A secondary "also…" is the first thing a summarizer drops |
| G7 | The summarizer's transcript caps each message at **4,000 chars (middle trimmed)** and keeps only the tail of the whole | `sidecall.py:13,50-54` | A long pasted list of asks loses its middle. On huge histories the earliest requests are cut before summarizing |
| G8 | Re-compaction summarizes the previous summary with no merge rule | `context.py:compact` | Losses compound. opencode tells the model explicitly: "anything you do not carry into the new summary is lost" |
| G9 | No todo tool and no completion rule. Plan mode writes a file, but nothing re-injects it. Instructions and skills live in the system prompt, so they do survive compaction | `agent.py:37,140` | Nothing tracks which asks are done; the model alone decides when to stop |
| G10 | Checkpoints store each prompt verbatim (≤16 KiB), and sessions persist them | `checkpoints.py:24,49` | A ledger can be built from data Relay already keeps |
| G11 | Recaps cover goal, completed work and blockers in 40–80 words. Instruction files are read-only; nothing is agent-writable | `suggestions.py:14`; `instructions.py` | No open-item list on return, and no cross-session memory |

## 2. Multiple requests: todo and plan tools, and completion rules

| Tool | Mechanism | Rules that keep asks from being dropped |
|---|---|---|
| Claude Code | `TaskCreate/Get/Update/List`; `TodoWrite` is legacy (`CLAUDE_CODE_ENABLE_TASKS=0`). On by default only for older models; newer models need `CLAUDE_CODE_ENABLE_TODO_TOOLS=1` because they "keep track of multi-step work without a written checklist, and the tools' definitions and reminders take up context" (https://code.claude.com/docs/en/tools-reference). "Tasks persist across context compactions". Shared lists via `CLAUDE_CODE_TASK_LIST_ID` in `~/.claude/tasks/`. Ctrl+T shows the list (https://code.claude.com/docs/en/interactive-mode) | (3P) "Use this tool proactively", "Mark it as in_progress BEFORE beginning work", "Mark tasks complete IMMEDIATELY", "Exactly ONE task must be in_progress", "ONLY mark a task as completed when you have FULLY accomplished it"; TaskUpdate: "Never mark a task as completed if: Tests are failing / Implementation is partial", "call TaskList to find your next task". Stale-list reminder: "The TodoWrite tool hasn't been used recently… gentle reminder - ignore if not applicable" (…/system-reminder-todowrite-reminder.md) |
| Codex | `update_plan {explanation?, plan:[{step, status: pending\|in_progress\|completed}]}`, "At most one step can be in_progress" (`codex-rs/core/src/tools/handlers/plan_spec.rs`). The TUI renders "Updated Plan" | `gpt_5_2_prompt.md`: "You must keep going until the query or task is completely resolved, before ending your turn"; "Do not jump an item from pending to completed… Do not batch-complete… **Finish with all items completed or explicitly canceled/deferred before ending the turn**… Do not let the plan go stale". `gpt_5_codex_prompt.md`: skip the plan for the easiest ~25% of tasks, and "Do not make single-step plans" |
| opencode | `todowrite` only; statuses pending, in_progress, completed, cancelled (`packages/opencode/src/tool/todowrite.txt`). Stored in SQLite `TodoTable` per session, each write replaces the whole list (`src/session/todo.ts`). **Not re-injected**: `compaction.ts` never references todos | "exactly ONE at a time", "don't batch completions", "Mark `completed` only after the required work is actually done… Never based on intent" |
| Warp | "For complex requests, the Agent generates a structured list of tasks". Chip at bottom right; active, completed, not started, cancelled (https://docs.warp.dev/agents/capabilities/task-lists/) | Persistence, user editing and completion rules: UNVERIFIED (not documented) |

Takeaway: the proven pattern is a model-maintained list with one item in progress, plus a hard rule that the turn
ends only when every item is completed or explicitly cancelled/deferred with a reason. Claude Code turning the list
off for its newest models is a caveat for frontier models. Relay's presets (Kimi K3, GLM-5.3, DeepSeek) are
open-weight models, and whether they track many asks without a list is UNVERIFIED. The eval in section 7 should decide.

## 3. Messages typed while the agent works

| Tool | Semantics | Injection and acknowledgment |
|---|---|---|
| Claude Code | Enter queues. "If you queue a message while Claude is running tool calls, Claude Code passes it to Claude as soon as those tool calls finish, within the same turn. When the turn ends with messages still queued, Claude Code sends only the oldest as the next turn." Esc interrupts "and sends it right away"; Up pulls queued items back; `/btw` asks a side question (https://code.claude.com/docs/en/interactive-mode) | Exact wrapper text: UNVERIFIED |
| Codex | "Press Enter while Codex is working to inject new instructions into the current turn"; Tab queues for the next turn (https://learn.chatgpt.com/docs/developer-commands.md?surface=cli). The TUI shows three groups: "Messages to be submitted after next tool call (press esc to interrupt and send immediately)", rejected steers "Messages to be submitted at end of turn", and "Queued follow-up inputs" (`codex-rs/tui/src/bottom_pane/pending_input_preview.rs`). Esc shows "Model interrupted to submit steer instructions" | Core keeps "Turn-local pending input storage" (`core/src/session/input_queue.rs`, `inject.rs`) |
| opencode | A prompt is persisted first, then `ensureRunning` joins the running loop, which exits only when `lastAssistant.parentID === lastUser.id`, so a mid-run message is picked up at the next step (`src/session/prompt.ts`). The web app forces "steer" (issues #24580, #44108) | Relies on persistence: the message is a stored user message before it is delivered |
| Warp | "One prompt is in flight at a time"; queued prompts are editable and deletable; Enter on an empty input sends the top one now; "Shell commands are never queued" (https://docs.warp.dev/agent-platform/local-agents/interacting-with-agents/prompt-queueing/) | Mid-turn steering: UNVERIFIED (not documented) |

Takeaway: all three steer-capable tools deliver at tool boundaries and keep undelivered steers for end of turn, as
Relay does. Their bug trackers show the risky code paths are dequeue-during-tool, interrupt and session switch (section 5).
Persisting the message *before* delivering it (opencode) is the robust design.

## 4. Long conversations: compaction and memory

**Claude Code.** Compacts at the context limit by default (~967K on native-1M models); `/autocompact`,
`CLAUDE_CODE_AUTO_COMPACT_WINDOW` (https://code.claude.com/docs/en/model-config). After compaction it re-reads the
root CLAUDE.md, unscoped rules, auto memory and the plan-mode plan from disk, "re-reads up to five" recently modified
files (over 5K tokens: path only), restores skill bodies (5K each / 25K total), and reminds Claude of running background
tasks "so it doesn't start a duplicate". Path-scoped rules and nested CLAUDE.md are lost until a matching file is read
(https://code.claude.com/docs/en/context-window). Hooks: PreCompact (can block), PostCompact (receives
`compact_summary`), SessionStart with source `compact` for re-injection (https://code.claude.com/docs/en/hooks).
**Summary prompt (3P):** an `<analysis>` scratchpad, then 1 Primary Request and Intent, 2 Key Technical Concepts,
3 Files and Code Sections, 4 Errors and fixes, 5 Problem Solving, **6 All user messages** ("List ALL user messages that
are not tool results… verbatim" for security-relevant instructions), **7 Pending Tasks**, 8 Current Work, 9 Optional
Next Step ("include direct quotes from the most recent conversation… verbatim to ensure there's no drift")
(…/agent-prompt-conversation-summarization-with-additional-instructions.md).

**Codex.** `prompts/templates/compact/prompt.md`: "You are performing a CONTEXT CHECKPOINT COMPACTION. Create a handoff
summary for another LLM that will resume the task", covering progress and key decisions, "constraints, or user
preferences", "What remains to be done (clear next steps)", and critical data. `summary_prefix.md` tells the next
model to "build on the work that has already been done and avoid duplicating work". `core/src/compact.rs`:
`COMPACT_USER_MESSAGE_MAX_TOKENS = 20_000`. The compacted history is the initial context + **recent user messages
verbatim, walked backwards up to ~20K tokens** + the summary. Auto limit is 90% of the window; config can only lower it
(`protocol/src/openai_models.rs`). The remote compaction path (`compact_remote_v2.rs`) is UNVERIFIED.

**opencode.** Overflow at input limit − `reserved` (min(20K, max output)); `compaction.auto` defaults to true
(`src/session/overflow.ts`). Optional pruning of old tool outputs (`PRUNE_PROTECT = 40_000`, `PRUNE_MINIMUM = 20_000`,
skill output protected). Keeps a 2K–15K token tail. Template: Objective / Important Details / Work State (Completed,
Active, Blocked) / Next Move / Relevant Files. "Preserve exact file paths, symbols, commands, error strings". It
**merges incrementally** with the previous summary. After auto-compaction it replays the last user message or says
"Continue if you have next steps, or stop and ask for clarification if you are unsure how to proceed"
(`packages/core/src/session/compaction.ts`). Todos are not carried over.

**Warp.** `/compact`, plus automatic summarizing past capacity
(https://docs.warp.dev/agent-platform/local-agents/interacting-with-agents/). Prompt and threshold: UNVERIFIED.

| Memory | Claude Code | Codex | opencode | Warp |
|---|---|---|---|---|
| Human-written rules | CLAUDE.md hierarchy (managed → user → project → `CLAUDE.local.md`), `@imports` (4 hops), `.claude/rules/` with `paths:` | `AGENTS.override.md` > `AGENTS.md` > fallbacks, git root → cwd, 32 KiB (https://developers.openai.com/codex/guides/agents-md) | First `AGENTS.md`/`CLAUDE.md` walking up; `instructions` globs/URLs (`web/src/content/docs/rules.mdx`) | Global Rules + `AGENTS.md`/`RELAY.md`; subdir > root > global; relevant rules pulled in (https://docs.warp.dev/agent-platform/capabilities/rules/) |
| Agent-written memory | **Auto memory**, on by default: `~/.claude/projects/<project>/memory/MEMORY.md` + topic files, first 200 lines/25 KB loaded; types user/feedback/project/reference; "Remember X"; `/memory` browses and toggles. The `#` shortcut is not on the current page (removal UNVERIFIED) (https://code.claude.com/docs/en/memory) | **Memories** (`/memories`): phase 1 at startup extracts `raw_memory`/`rollout_summary` from idle past sessions with secrets redacted; phase 2 is a no-network consolidation subagent that maintains `~/.codex/memories/` (`MEMORY.md`, `memory_summary.md`, `skills/`), ranked by usage (`codex-rs/memories/README.md`) | None | **Agent Memory** (research preview): facts extracted at conversation end, retrieved at task start, shared across Warp, Claude Code and Codex cloud runs (https://docs.warp.dev/agents/agent-memory/) |

## 5. Reported failure modes (verified titles/states on GitHub, 2026-09-17)

| Product | Issue | Pattern | Mitigation seen |
|---|---|---|---|
| Claude Code | #86614 (closed) "Queued message is dequeued while a tool call is still in flight and silently discarded" | dropped queued message | fixed; keep a record of every queued message |
| Claude Code | #77010 (open) queued messages lost on session switch; #6354 (open) "forgets everything in CLAUDE.md after compaction"; #13955 forgets plans | lost queue; forgotten rules and plans | root CLAUDE.md and plan re-read from disk after compaction |
| Codex | #5957 (open) "Auto compaction causes GPT-5-Codex to lose the plot… forgets it is mid-task, forgets it has edited files and stops" | stops after compaction | carries user messages verbatim; "avoid duplicating work" prefix |
| Codex | #25792 (open) forgets AGENTS rules, progress 97% → 42%; #35226 compaction loop re-reads files; #43819 (open) queued messages disappear or become steers; #32627 a handled steer is re-acknowledged after compaction | repeated work, lost or duplicated messages | — |
| opencode | #5934 (closed; commenter says it persists) todo list forgotten after compaction; #42082 todo state never flows back to the model (closed on process grounds); #40955 (open) "Queued messages are silently dropped when a turn is interrupted"; #41358 loses the original goal; #37627 reload AGENTS.md and skills after compaction | exactly Relay's G2, G5, G6, G9 | proposed: inject todos as reminders |
| Warp | #7199 (closed) "RELAY.md and Agent Rules are ignored once the context window is reached or after summarization"; #7408 plan and subagent tracking breaks after a few subtasks | forgotten rules and plans | — |

Lessons: (1) every message path (queue, steer, interrupt, session switch) needs a durable record and a test; (2) a
summary alone loses goals, so re-inject state from a source of truth rather than hoping the summarizer keeps it;
(3) mark *handled* messages, so compaction does not make the model redo or re-acknowledge them (Codex #32627).

## 6. Recommendations for Relay

Principle: the **request ledger is the source of truth**, kept by the worker deterministically, never by a summarizer.
The model maintains todos linked to ledger entries. Compaction and reminders render from the ledger, so a request
cannot be summarized away.

| # | Pri | Change | Size |
|---|---|---|---|
| 1 | P0 | **Close the drop paths (G1–G3, G5, G7).** Raise the default turn limit (e.g. 60 steps / 150 tools, configurable). At the limit, end with outcome `limit` (not `error`), so the queue is not paused, and show "Continue". On cancel or error, keep the user message (and delivered steers) and remove only the incomplete tool group, then note "The turn was stopped at the user's request; the request above is not finished." Frame steers (see sketch) and pass their attachments and context through `format_attachments`. `turn_starts` counts only turn-opening prompts (tag messages with `relay_kind`). Exempt user messages from the 4K per-message cap in summarizer input | S |
| 2 | P0 | **Request ledger** (`backend/relay_core/requests.py`, saved in the session). Every `ask`, queued, interrupt and steer prompt becomes `R<n>` with verbatim text, source (`ask`, `steer`, `queue`, `interrupt`, `relay`), turn, time and status (`open`, `done`, `cancelled_by_user`, `blocked`, `deferred`). Created at submission (before delivery), so a queued, steered or interrupted prompt always exists. Relay-origin items (subagent finished) are tracked but do not require completion | M |
| 3 | P0 | **Todo tool with request links.** `update_todos {items:[{id?, text, status, request_ids:[R3], note?}]}`, one `in_progress`; statuses as opencode plus `deferred` and `blocked` (reason required). Prompt rule (Codex wording adapted): when a message contains more than one ask, *or* a steer arrives, create one todo per ask before working, with the ask quoted; keep going until every todo is completed, cancelled or deferred with a reason. Stored in the session; emitted as a `todos` event. Replaces feature #9 in `AGENT-FEATURES-RESEARCH.md` Part II | S |
| 4 | P0 | **Compaction that cannot lose asks.** (a) After the summary, the worker inserts a deterministic block: **all ledger requests verbatim** (open ones in full; done ones capped at 400 chars with a "full text: `request_get R3`" pointer), the todo list, the active plan path, the files touched this session, running subagents. (b) Carry recent user messages verbatim up to ~20K tokens (Codex). (c) Summary sections become Requests and intent / Decisions and constraints (user wording quoted) / Files and code / Errors and fixes / Work state (completed, active, blocked) / Next step (quote the latest request). Merge with the previous summary using opencode's "anything you do not carry over is lost" rule. (d) Mark handled steers as handled in the block (Codex #32627) | M |
| 5 | P1 | **End-of-turn completion check.** When the model answers with no tool calls and the ledger or todos still have `open`/`in_progress` items linked to this turn, the worker does not end the turn. It appends one reminder and requests again: "Before finishing: R2 'also update the docs' and todo 3 are still open. Do them, or mark them cancelled/deferred/blocked with a reason via update_todos." At most 2 reminders per turn, then the turn ends with `done {open_items:[…]}` and the UI shows them. No extra model call | S |
| 6 | P1 | **Ledger UI.** A "Requests 3/5" chip in the queue strip. The expanded list shows each request with ✓ ◐ ○ ✕ ⏸, linked todos and deferred reasons. The user can mark done or cancelled, or "Re-ask" (queues the verbatim text). On resume and in away recaps, list open items ("2 requests still open"). `sessions` listing gains `open_requests` | M |
| 7 | P1 | **Stale reminders.** If no `update_todos` call has happened for 8 steps while todos are open, add a short reminder at the step boundary (Claude Code pattern, text in the prompt, not a model call) | S |
| 8 | P1 | **Optional audit side call** (setting, off by default). On `done`, a cheap no-tools call (the route-assist model) receives the turn's user messages and the final answer and returns `{unaddressed:[{request_id, quote}]}`. Results are only flagged in the UI ("may be unaddressed: …"), never auto-continued. This catches asks the model never put in a todo | S |
| 9 | P2 | **Project memory.** `remember {text, scope: project\|user}` writes to `<workspace>/.relay/memory.md` or `~/.config/relay/memory.md`. Every write shows a toast plus diff and can be undone. Files are plain Markdown the user edits; loaded after instruction files (cap 8 KB, first 200 lines like Claude Code) and re-read at compaction. Keep roles separate: **RELAY.md/AGENTS.md** = human-authored rules (read-only for the agent unless asked); **memory.md** = durable facts and preferences the agent learned ("tests need Xvfb", "owner prefers terse answers"); **Board cards** (`issues/feature_intake.txt`, `docs/SCRATCHPAD-DESIGN.md`) = work items. The bridge: at session end or `/new`, open or deferred ledger items can be offered as Board cards ("Save 2 open requests as cards?"), and a card's thread seeds a ledger when started | M |
| 10 | P2 | **Cross-session memory consolidation** (Codex phase 1/2 style): an opt-in background pass over idle sessions proposes memory edits for review (Devin/Gemini "suggest, don't write" pattern from `SCRATCHPAD-DESIGN.md`) | L |

### Protocol sketches (additive to `docs/AGENT-SESSIONS-PROTOCOL.md`)

```
→ ask {id, text, when, attachments?}         ← queued {id, request_id, ledger_id: "R7", when, position}
← requests {items:[{id:"R7", text_preview, source:"steer", turn_id, status, todo_ids:[...], updated}]}
→ request_get {id:"R7"}                      ← request {id, text (verbatim), source, turn_id, status, reason?}
→ request_set {id:"R7", status:"cancelled_by_user"|"open"|"done"}   ← requests {...}
← todos {turn_id, items:[{id:"T3", text, status, request_ids:["R7"], note?}]}
← completion_check {turn_id, open:[{id:"R7", preview}], reminder: 1}      (worker re-prompted)
← done {turn_id, open_items:[{id, preview, status}]}                      (unchanged otherwise)
← compacted {..., carried: {requests: 7, open: 2, todos: 5, user_message_tokens: 18234}}
← recap {..., open_items:[...]}; sessions items gain open_requests
```

Steer delivery text (model-facing):

```
[Sent by the user while you were working (R7, 14:02). Keep your current task (R5) unless this changes it.
Add it to your todos if it is a new ask, and say briefly how you handled it in your final answer.]
<verbatim text + attachments>
```

Post-compaction block (built by the worker, not the model):

```
[Relay state carried across compaction: authoritative, not summarized]
## User requests (verbatim) — R1 done · R2 open · R5 in progress · R7 open (steer)
R2: "…full text…"
## Todos  ## Active plan: .relay/plans/…md  ## Files touched  ## Running subagents
```

## 7. Tests and evals

- **Unit tests (fake provider, `tests/test_requests.py`)**. Each drop path gets a regression test:
  - steer during a tool keeps attachments;
  - interrupt keeps the interrupted request in `messages` and the ledger;
  - cancel then resume keeps queued items;
  - hitting the step limit does not pause the queue;
  - auto-compaction after 3 steers still contains the turn's original prompt verbatim;
  - a 30-message conversation compacted twice contains every `R<n>` text;
  - the completion check re-prompts at most 2 times;
  - `load_state`/`resume` restores the ledger and todos;
  - `steer_returned` requeues exactly once.
- **Scripted scenario eval** (`scripts/eval-requests.py`, fake and live providers, run like the live evidence in
  `docs/qa_evidence/`). A temp workspace and scripted user actions with timing. Scenarios:
  1. one message with 5 numbered asks (create files a–e);
  2. an unnumbered "fix X, and also rename Y, oh and update the README";
  3. 3 steers sent during a long tool run;
  4. an interrupt with a new ask, then "continue the earlier one";
  5. queued prompts plus a failure;
  6. a forced small window (e.g. `context_window` 16K) so compaction happens 2–3 times, with a constraint given at turn 1
     ("never edit tests/") checked at turn 20;
  7. a resume after restart;
  8. a single simple ask, which should produce **no** todo list (added 2026-09-17: with the request-as-task backfill
     gone — card `H3QW` — an empty list is what keeps the Tasks chip hidden, so it has to be measured, not assumed);
  9. scenario 1's five asks plus a mid-turn steer that *refines* one of them, which should join the existing todo
     rather than add a sixth (added 2026-09-17 with the matching `todos.RULES` sentence).

  Checks are deterministic: files and contents on disk, ledger status, the constraint never violated, no duplicate
  work (the same file written twice with the same content, repeated commands). Metrics: asks completed / asks given,
  silent drops (open with no reason and not flagged), false "done", and (added 2026-09-17) `todo_calls`/`todo_items`,
  the todo-tool uptake the Tasks UI now depends on. **First results**, 2026-09-17, in
  `docs/qa_evidence/2026-09-17-tasks-are-todos-only/eval/RESULTS.md`: all three keyed presets write a complete list for
  a multi-ask prompt and none for a single simple ask, which settles the "do Relay's open-weight models need the list"
  question below in the affirmative for uptake; GLM-5.3 drops the list when a steer arrives (own card). Run per preset (Kimi K3, GLM-5.3, OpenRouter
  DeepSeek), 3 runs each, with and without the todo tool (to test Claude Code's "newer models don't need it" claim on
  Relay's models).
- **GUI QA**: Xvfb run checks that the ledger chip counts, steer toast and requeue, and open items on recap all match
  the events.

## 8. Order of work

1 (drop fixes + tests) → 2 + 3 (ledger, todo tool, `requests`/`todos` events) → 4 (compaction block) → 5 + 7
(completion check, reminders) → eval baseline before and after → 6 (UI) → 8 → 9 alongside the Board → 10.
Open questions for the owner: default turn limit; whether the completion check may re-prompt automatically (proposed
yes, max 2); whether memory lives in the repo (`.relay/memory.md`, shareable like the Board) or per user.

## 9. Owner decisions (2026-09-17)

- **Do all recommended fixes.** Items 1–8 are being implemented now. The todo tool (item 3) ships in the minimal
  form above; the user-facing task list is redesigned in a separate proposal (Warp vs Claude Code, integrated with the
  Board: a task can be a whole card or an item on a card).
- **Step limit:** default 50 model steps (tool-call cap 150), configurable in Agent options.
- **End-of-turn check:** yes, automatic re-prompt, at most 2 per turn.
- **Project memory:** lives in the repo's Board, viewable and editable alongside plans and issues, with private
  (not synced to git) memories, like private cards. Supersedes the `.relay/memory.md` location in item 9.
