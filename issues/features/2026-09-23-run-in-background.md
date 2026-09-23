---
id: BGRN
type: work
status: planned
labels: [feature, panes, switchboard, notifications]
rank: m
created: '2026-09-23'
source: Owner in a Relay pane (520ccb90), 2026-09-22 20:11 to 2026-09-23; discussed with Codex, card written by Claude Code
links: {plans: [], commits: [], evidence: [], related: [RG0Z], github: null}
---
# Run in background: hand a task to an agent, get the pane back, hear only when it needs you or is done

## Issue
> help me plan and think about the following. what if, i can send a task and say, the agent delivers it and the pane minimizes or goes into an invisiible queue or something, and then only returns if it needs me. otehrwise when its done, i jsut get a done notification.

> think about, should this be from panes, or the switchboard (execute) or both.

> the hidden jobs show up in the header, at the top right, next to the notifications. you have numbers there corresponding to the number of hidden jobs that are working (pulsing violet), "needs you" (flashing amber),  done (still green), failed (flashing red). plus the notifications. so you can get to these items either way.
>
> and if you do send and hide but the agent isnt ready, it will tell you (similar to how when you execute and it hasnt been planned, you get the notification).
>
> while we are here, lets discuss the terminology for deliver, execute, send & hide, etc, to make things clear and intuitive.

> i like this terminology. write the plan on a card

## Decisions
- **Both entry points, Board first.** Board **Run** (today's Execute) goes to the background by default, with **Run in pane** as the alternative. In a pane, backgrounding is an explicit handoff: **Run in background** for a new task, **Move to background** for one already running. Both share one background list, one set of header counts and one notification path, and opening from either reveals the same live session with its context.
- **Header counts (owner's spec):** four counts to the left of the bell. Working is pulsing violet, needs you is flashing amber, done is steady green, failed is flashing red. A count at zero is hidden. Clicking a count lists those tasks. The counts show current state and the bell records events; either one opens the session.
- **Readiness check before hiding.** The pane stays visible until the agent accepts the handoff. If it can't start ("One decision needed before I can start: …" or "This needs a plan first. [Plan]"), it says so in the pane, the way Execute warns about an unplanned card. A small, clear task passes straight through without a plan.
- **Done means done.** Green only when the requested outcome is complete. The end of a turn, a wait on a subagent, an intermediate step or an error is never success. Work awaiting required verification stays violet, or turns amber if the next step is the owner's.
- **Terminology (owner: "i like this terminology"):**

  | Label | Meaning |
  |---|---|
  | Send | Message the agent; the conversation stays visible |
  | Plan | Work out the approach and settle the key decisions |
  | Run | Carry out the task (replaces **Execute** everywhere) |
  | Run in background | Run it and hide the pane once the agent accepts it (replaces "Send & hide") |
  | Move to background | Hide work that is already running, keeping its context |
  | Open | Bring a background session back into view |
  | Verify | Check the result against what was asked |

  **Deliver** stays the `/deliver` workflow (plan as needed, then implement and verify). It gets no button of its own until its completion promise is precise. Delivery and background visibility stay independent.
- **First version:** background work lives as long as Relay stays open. After a restart, interrupted background work is listed as interrupted and can be recovered; it is not silently lost.
- **Run's key is `r`; `x` is removed**, with no alias (owner: "1 yes, and you can go ahead and remove x.").
- **Green done tasks clear once opened** (owner: "2 yes.").
- **What "done" means, for built-in and guest agents alike:** a background task is green when every request in its request ledger (`backend/relay_core/requests.py`) is `done` (the turn ended normally and every linked todo is completed), no todo it owns is open, and no subagent it started is still running. A card-backed Run is green only when the card reaches `needs-verification` or `done`. Anything cancelled, blocked or deferred goes to amber with its reason.

## Done means
- From a pane, Run in background on a ready task removes the pane from the layout (neighbours expand), the violet count goes up, and the agent keeps working. Move to background does the same for a running turn without losing its conversation or terminal.
- A question or blocked state moves the task to amber and posts one notification, without stealing focus. Answering continues it in the background. A task that finishes its request turns green with one "Done: …" notification; a failure turns red with the reason. No intermediate turn ever shows green.
- Board Run backgrounds by default and Run in pane keeps today's behaviour. Clicking a count, a notification or the card's Open session reveals the same live session.
- A task that isn't ready never hides: the pane says why and offers the next step.
- "Execute" appears nowhere in the UI, hints or docs; it reads "Run". Failure shows up as a hidden pane with no count, a count that disagrees with the list, a duplicate notification, or a green count while the work is unfinished.

## Plan
**Goal:** Run in background from panes and from the Board, header counts for background work, readiness and completion semantics you can trust, and the Plan → Run → Verify terminology.

**Findings:**
- `src/PaneStatus.h:39` already has `State::Working`, `Failed` and `NeedsYou` per pane. The background states map onto these, plus a new task-level `Done`, which is not the same as a turn ending.
- `src/RelayWindow.h:9936` builds `m_bell` in the right chrome row; the badge refresh is at `:10115`, and `NotificationsPopup` is in `src/WindowChrome.h:304`. The counts go in the same `rightRow`, before the bell.
- `src/RelayWindow.h:7201` `detachTab` moves a live page out; its current path can close a window left empty, so it is not a background owner as it stands.
- `src/Pane.h` posts completion and question notifications per turn. Those need to become per task for background work, deduplicated.
- **Completion already exists, and guests share it.** Every prompt becomes a request `R<n>` in `backend/relay_core/requests.py`. At a turn's final answer, `Agent._open_items` (`backend/relay_core/agent.py:3312`) lists requests with open linked todos. While any remain, Relay injects a `[Relay completion check n/2]` reminder and the turn continues (`agent.py:2194`, `MAX_COMPLETION_REMINDERS = 2`). Only then does `finish_turn` mark requests `done` (`requests.py:178`). Guests go through the same `Agent` loop (`guest_harness_provider.py` header), so Claude Code and Codex get the same ledger and checks.
- **The gap for guests:** Relay only sees todos written through `relay_board`'s `update_todos` (`guest_board_bridge.py:29`), and to a guest that tool is optional. Without it, a guest's request goes `done` as soon as the turn ends normally.
- **Claude Code (2.1.280):** its list tools are `TaskCreate`/`TaskGet`/`TaskUpdate`/`TaskList`, and `TodoWrite` is legacy. They are off by default on current models unless `CLAUDE_CODE_ENABLE_TODO_TOOLS=1` is set (`docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md:49`), which is why a Claude guest here has none. `claude --disallowedTools` can guarantee they stay off. `guest_harness_claude.py:_argv` passes no such flag today.
- **Codex (0.156.0):** `update_plan` is built in, and there is no config or feature flag to turn it off: `disabled_tools` applies only to MCP servers and plugins, and `codex features list` has no plan entry. Each call reaches the app-server client as a `turn/plan/updated` notification with steps and `pending`/`inProgress`/`completed` statuses (from `codex app-server generate-json-schema`). `guest_harness_codex.py` does not handle it today.
- Board Execute: `src/BoardPane.cpp:2599` `execute()` (with the unplanned-card warning at `:2613`) and the action label at `:3152`; `src/BoardModel.cpp:344`; the plan pane's `Execute` and `Execute in fresh context` buttons at `src/FilePanes.cpp:1916`; `src/AgentContext.h:96,129` document the label.
- #RG0Z ("dim while working") dims the pane in place. This card frees the layout space, and dimming stays as it is for panes left visible.

**Steps:**
1. **Lifecycle model.** Add a background-task record (pane id, title, card id if any, request ids, state working / needs-you / done / failed / stopped / interrupted, last reason) owned by `WindowManager`. Transitions follow the done rule under Decisions, not turn ends. Unit-test the transitions, including subagent waits, cancelled, blocked or deferred todos, and card-backed tasks awaiting needs-verification.
2. **Background ownership.** A hidden holder that keeps a `Pane` alive (pty, worker, project context) outside any layout, and a restore that reinserts it into the current window. Hiding the last pane in a window leaves the window open with an empty-state view; it never closes it.
3. **Pane entry points.** A **Run in background** send variant in the composer and **Move to background** on the pane header, in Actions and as a keymap action with a shortcut hint. Both are gated by the readiness check (step 5).
4. **Header counts and list.** Four count chips in `rightRow` before `m_bell`, with the owner's colours and animations (theme-aware, respecting reduced motion). Clicking one opens a popup like `NotificationsPopup`, filtered to that state, with Open and Stop per task. Opening a done task clears it. The chips are keyboard-reachable and appear in Actions.
5. **Readiness check.** Before hiding, the agent must accept: no open question to the owner, a plan if the task needs one. Otherwise the pane stays and shows the reason and a **Plan** action. Share the logic with the Board's unplanned-card check.
6. **Completion and notifications.** Worker: add a `background` flag on the request, and a request event that reports ledger status, open todos and running subagents per request so the GUI can apply the done rule. One notification per state change of a background task (needs you, done, failed), deduplicated against the per-turn notices, which are suppressed while the pane is in the background. A notification click opens the session.
7. **Guests get the same completion signal: Relay tasks are the only list.** (a) Claude Code: add `--disallowedTools TaskCreate TaskGet TaskUpdate TaskList TodoWrite` to `_argv`, so `relay_board` `update_todos` is its only task list. (b) Codex: since `update_plan` cannot be turned off, the harness turns each `turn/plan/updated` into a Relay `update_todos` for the current request (steps become todos; `inProgress` maps to `in_progress`), so Codex's native habit feeds the same ledger. Its guest instructions also say to use `update_todos`; the mirror only fills in when it doesn't. (c) For a background request, the turn context says the task list is required and that the turn does not end with items open. The existing completion check (`agent.py:2194`) then applies unchanged.
8. **Board Run.** Rename Execute to Run on key `r`, and remove `x`. Run backgrounds by default, **Run in pane** keeps the old behaviour, and a card with a background session shows its state and **Open session**. Check that `r` is not already taken on the card page, and that the shortcut hints change with it.
9. **Terminology sweep.** Rename Execute to Run in `BoardPane.cpp`, `BoardModel.cpp` (keeping the stored `execute` action key for old thread entries), `FilePanes.cpp` ("Run", "Run in fresh context"), tooltips, keymap descriptions, shortcut hints, `docs/` and `backend/relay_core` prompts and policy text. The internal names can stay.
10. **Restart recovery.** Persist the background list with the session id; on launch, list tasks that were running as *interrupted*, with Open to resume.

**Risks:**
- A guest that ignores its todo list still ends its request `done` when the turn ends. Step 7 removes Claude's own list and mirrors Codex's, so each keeps a Relay-visible list as it normally works. For a task with no todos at all, done means "the turn ended normally" for built-in and guest agents alike: the same promise Relay makes today.
- A hidden pane must keep its pty draining. A stalled reader would freeze the agent's shell.
- `Pane.h` and `RelayWindow.h` are heavily shared. Land through `scripts/land.py` in small commits, one step at a time.

**Verify:** a `backgroundtasks` unit test for the lifecycle, dedup and readiness gate; pytest for the request event, the Claude `--disallowedTools` argv, and Codex `turn/plan/updated` mirroring, with the fake harness; targeted `panes` and `panestatus` tests; an Xvfb run with an isolated config showing hide, counts, a question going amber and being answered, done then cleared on open, failure, hiding the last pane, restore into a changed layout, a notification click, and Board Run (`r`) versus Run in pane; one built-in and one guest agent live; `rg -g '!issues/' '"Execute'` finding no UI string.
