---
id: K54A
type: work
status: needs-verification
labels: [bug, prompt]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: dd2f0e10-c7bd-40e0-b92a-0c0d6e91c93d
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
links: {plans: [], commits: [d2e53a077d1b], evidence: [docs/qa_evidence/2026-09-25-k54a-prompt-budget/NOTES.md, docs/qa_evidence/2026-09-25-k54a-prompt-budget/], related: [BYHN], github: null}
---
# System prompt is 497 bytes over its budget; the size test is red at HEAD

## Issue
tests.test_system_prompt.SizeTests.test_the_prompt_and_the_tools_stay_within_their_budgets fails at git HEAD 2f848bec with "system prompt grew to 7153 bytes" against the 6 KiB + 512 budget (6656). Reproduced on a clean copy of HEAD's board.py, board_tools.py, board_policy.md and tools.py: the number is identical (7153), so no uncommitted work causes it. Measured 2026-09-25 while landing #EMWF; the comment in the test says 5.9 KB was measured 2026-09-20 (#GMCF), so a landed change since then grew the boardless system prompt by ~500 bytes past its budget. Fix: find the growth (likely a prompt block added without the budget test run) and either trim it or make the next budget a stated decision.

## Done means
`python3 -m pytest tests/test_system_prompt.py` is green at HEAD, in particular `SizeTests.test_the_prompt_and_the_tools_stay_within_their_budgets`: the boardless prompt is under 6 KiB + 512 and the tool list under 18 KiB (measured 7,153 and 18,887 bytes on 2026-09-25). The measurement comment in the test is updated to the new date and numbers. Failure looks like the red we have now: `system prompt grew to 7153 bytes` / `tool schemas grew to 18887 bytes`. If the right answer is a larger budget, the card carries an owner decision saying so and the test's constants and comments move together — no silent bump.

## Plan
**Goal.** Bring the boardless system prompt (7,153 B vs 6,656 budget) and the tool list (18,887 B vs 18,432) back under their SizeTests budgets by trimming what grew since 2026-09-20, or get an explicit owner decision to raise the budgets.

**Findings.** `tests/test_system_prompt.py::SizeTests.test_the_prompt_and_the_tools_stay_within_their_budgets` measures `Agent.system_prompt()` and `json.dumps(agent.tools())` on a fixture with three skills and the real keybinding registry; the comment records 5.9 KB prompt / 15.8 KB tools measured 2026-09-20 (#GMCF). The boardless prompt is assembled in `backend/relay_core/agent.py::Agent.prompt_sections` from: `SYSTEM` (agent.py), `todo_tool.RULES` (todos.py), `app_tools.prompt_section` (app_tools.py:1588), `activity_tools.prompt_section` (activity_tools.py:396), the skills catalogue, `tool_groups.prompt_line`. The tool JSON includes the two schemas landed since the measurement: `land_try` (#76QW, e491e055, ~662 B) and `agent_stop` (#FYEY, 12ca8565). A per-section / per-tool breakdown tool already exists: `docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py` — the test file's docstring says these tests are its assertions.

**Steps.**
1. Reproduce: `python3 -m pytest tests/test_system_prompt.py::SizeTests -x`; record both byte counts.
2. Attribute the growth: run `docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py` (or the same breakdown inline) for per-section and per-tool bytes, and `git log --since=2026-09-20 --oneline -- backend/relay_core/agent.py backend/relay_core/app_tools.py backend/relay_core/activity_tools.py backend/relay_core/todos.py backend/relay_core/tool_groups.py backend/relay_core/tools.py backend/relay_core/prompt_profiles.py` to name the commits that added the ~1.1 KB across prompt and tools.
3. Trim to fit, newest additions first: tighten the `land_try` and `agent_stop` schema descriptions and whichever prompt section grew, cutting redundancy — not rules. Where a sentence restates what a tool's schema already says, the schema is the keeper (#GMCF decision 6 is the model). Keep every rule the prompt carries; move detail into tool descriptions only where the model reads it after choosing the tool.
4. Update the measurement comment in the test (date and numbers) and confirm all three assertions (boardless prompt, tools, board prompt) pass.
5. If the trim cannot reach budget without dropping a rule worth keeping, stop and ask the owner before touching the constants: the budget exists so its growth is a decision, per the class docstring.

**Risks.** Other tests in `tests/test_system_prompt.py` assert on prompt text (phrase checks, byte-stability across turns/restarts), so run the whole file, not just SizeTests. The prompt is prefix-cache-sensitive: trim by deleting whole sentences, not by rewording stable text, where the saving is equal. Open question for the owner if trimming falls short: raise the budgets (prompt 6 KiB + 512 → 7.5 KiB, tools 18 KiB → 20 KiB) as a stated decision, or accept weaker tool descriptions.

**Verify.** `python3 -m pytest tests/test_system_prompt.py` green (whole file). Attach the promptsize.py before/after breakdown as evidence.

## Tests
- `python3 -m pytest tests/test_system_prompt.py`: 33 passed in this checkout, and again on a clean `git archive d2e53a07` with `PYTHONPATH` set to the export and an empty `HOME`.
- Sizes (budget): boardless prompt 7,011 → 6,335 (6,656); tool list 18,894 → 17,879 (17,920 and 18,432); prompt with a board 10,451 → 9,538 (9,728); policy block 3,302 → 3,065 (3,072). Reproduce with `python3 docs/qa_evidence/2026-09-25-k54a-prompt-budget/measure.py`.
- `tests/test_board.py::PolicyFileTests::test_this_repositorys_own_policy_is_a_fresh_regeneration` passes in the working tree after `relay-board.py policy`. At the tip it still fails, because the committed `.board/POLICY.md` predates the `issues/` → `.board/` move. That regeneration is session `c8xd`'s uncommitted work, so only this card's three-line removal was landed.
- Neighbours (`test_board`, `test_board_tools`, `test_tools`, `test_jobs`, `test_scratch_ledger`, `test_user_memory_tools`, `test_guest_memory`, `test_prompt_profiles`, `test_guest_board_bridge`): 711 passed, 3 failed. All 3 fail identically on a clean HEAD export: the short-profile board budget (filed as #BYHN), guest bridge parity, and guest_memory's Mock.
