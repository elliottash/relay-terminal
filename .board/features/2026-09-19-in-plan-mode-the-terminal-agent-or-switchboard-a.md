---
id: K3TY
type: work
status: needs-verification
assignee: agent
priority: 2
rank: zzzzzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [4a0cb8cd, 6011141c], evidence: [docs/qa_evidence/2026-09-20-plan-orchestration-block/], related: [], github: null}
---
# in plan mode, the terminal agent or switchboard agent should plan orchestration…

## Issue
in plan mode, the terminal agent or switchboard agent should plan orchestration (see how warp does this)

## Plan
**Goal**
Both plan-mode authors — the terminal pane's `write_plan` turn and a Switchboard card's Plan turn — should decide not just *what* changes but *how the work is executed*: when the job is big enough to split, the plan carries an **Orchestration** block (which steps go to subagents, which run in parallel, which wait), and the Execute prompts tell the executing agent to follow it. Warp-style: the orchestration is part of the plan the user approves (Relay's approval is Execute itself).

**Findings**
- Terminal plan mode: `backend/relay_core/planning.py` — `PLAN_MODE_NOTE` ends at "goal, findings, numbered steps, risks, how to verify"; nothing about execution. `execution_prompt()` (end of file) is only `Execute the plan in <path>:`.
- Subagent tools are withheld in plan mode (`backend/relay_core/agent.py:647`; the response batch too, `:1372-1373`); an Execute turn runs in build mode with the full toolset, including `agent`/`agent_message`/`agent_wait` (`backend/relay_core/subagents.py:61`, specs `:414-449`).
- Card Plan turn: `backend/relay_core/board_plan_brief.md` (v1, #XS6Q) — five bullets, nothing on execution. Plan-turn scope enforcement in `board_tools.py:1278-1323` is separate and unchanged.
- Card Execute: `board::executeTask` (`src/BoardModel.cpp:361`) builds "Execute #ID … Carry out the plan …" in both hasPlan variants (lines 369, 372); the wording is pinned by `tests/boardmodel_test.cpp:2115, 2144-2156, 2302, 2533`.
- Machinery a plan can name: built-in subagent types `explore`/`general` (`agents_defs.py:91-107`); todo↔subagent links via `todo_id` (`agent.py:507-545`).

**Steps**
1. `planning.py` `PLAN_MODE_NOTE`: extend the `write_plan` instruction — when the work is big enough to split across subagents, the plan includes an **Orchestration** block: each subagent (type, one-line task), which steps run in parallel, which wait for which. Only steps that touch no shared files may be parallel; writes stay on the main agent. Small plans get no block — no ceremony.
2. `planning.py` `execution_prompt()`: append one standing line — where the plan has an Orchestration block, follow it: start the listed subagents (independent ones in one response, so they run concurrently), wait before dependent waves, do yourself only what the block assigns to the main agent, and name any deviation in the final reply.
3. `board_plan_brief.md` → v2 (#K3TY): add an **Orchestration** bullet to "What a plan holds", same rule as step 1, written for the terminal agent that Execute hands the card to.
4. `src/BoardModel.cpp` `executeTask`: in both hasPlan variants, one sentence after "Carry out the plan …": follow the plan's Orchestration block where present (parallel steps to subagents started together, dependent waves in order). No-plan variants unchanged.
5. Tests: extend `tests/test_questions.py:301` (note names Orchestration), assert the new `execution_prompt` line in `tests/test_sessions.py` `PlanModeTests` (`:406`), extend the brief check in `tests/test_board_protocol.py:891` (`test_a_plan_needs_no_words_and_carries_the_plan_brief`), and update the pinned `executeTask` assertions in `tests/boardmodel_test.cpp` (`:2115`, `:2144-2156`, `:2302+`, `:2533+`). Build through `scripts/relay-build`.
6. Docs, one line each: `docs/AGENT-SESSIONS-PROTOCOL.md` (plan-mode `write_plan` bullet and 19.10 Execute) and `docs/SWITCHBOARD-DESIGN.md` (Plans bullet) — plans may carry an Orchestration block; Execute follows it. No new messages or events.

**Risks**
- Concurrent subagents share the workspace (no worktrees); the per-path SHA lease turns a collision into an error, not corruption. The briefs must keep parallel steps read-heavy and writes on the main agent.
- Prompt-only: the executor can still ignore the block (Warp enforces with an orchestration engine). Enforcement — Execute seeding the plan's steps as todos handed out via `todo_id` — is a separate, larger card if wanted.
- **Settled in thread (owner pushed back, agent conceded):** plan-mode turns *should* also be able to run read-only subagents (Claude's plan mode does; the withholding comment at `agent.py:652` holds only for write-capable definitions like `general`, not `explore`, whose toolset is `READ_ONLY_TOOLS`). That is a **separate small card**, not folded into this prompt-level one: filter plan-mode subagent specs to read-only definitions and un-gate the two `mode != "plan"` checks (`agent.py:653` tool filter, `:1376-1377` batch start). Write-capable (`general`) subagents stay out of plan mode — plan mode's promise is that nothing changes before Execute.

**Verify**
- `python3 -m pytest tests/test_questions.py tests/test_sessions.py tests/test_board_protocol.py -q`, and `ctest --test-dir build -R boardmodel` after `scripts/relay-build`.
- Live under Xvfb: plan a multi-part change in a pane (Shift+Tab → PLAN) and confirm the written plan has an Orchestration block; Execute and watch the subagents pane start the planned agents together. On a card, `p` then `x`, and confirm the executing pane follows the block.

## QA checklist
- [ ] `PYTHONPATH=backend python3 -m unittest discover -s tests -p 'test_questions.py'` and `-p 'test_sessions.py'` — green (35 and 37). `-p 'test_board_protocol.py'` green except the 2 auto-stage-move failures that reproduce byte-identically at pre-card HEAD (lists and analysis in the evidence README).
- [ ] `QT_QPA_PLATFORM=offscreen build/relay-board-tests theExecuteTaskCarriesTheBoardsConventions theExecuteTaskAsksForTheImplementedByTrailer theBriefsAskForTheExactModelAndTheGuestHarness executeHandsTheCardToAPaneAndMovesItToExecuting` — 6 passed, 0 failed.
- [ ] Live, a pane: Shift+Tab → PLAN, ask for a multi-part change — the written plan carries an Orchestration block (subagent types, parallel and dependent steps, writes on the main agent); Execute it and the subagents pane starts the planned agents together, dependent waves waiting.
- [ ] Live, a card: `p` writes a plan with an Orchestration bullet (Plan brief v2); `x` hands a task saying "Where the plan has an Orchestration block, follow it", and the executing pane follows it, naming any deviation when it finishes.
