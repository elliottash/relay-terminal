---
id: M7RY
type: work
status: needs-verification
labels: [feature, memory, globals]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mumry
created: '2026-09-22'
source: Codex in Relay, 2026-09-22
links: {plans: [], commits: [6d8cbe29d48a65b8dbf28e3a6422145cc77d76fa], evidence: [docs/qa_evidence/2026-09-22-user-memory/], related: [Y2MP, P7SJ], github: null}
---
# User memory research, review and interview in Globals

## Issue
i want to do deep research on what relay should know about the user. see how warp, claude code, codex, opencode, etc. deal with this issue. and add a specific memory section to the globals tab on what it knows about the user, so the user can review / edit it. also make a helper agent there taht will interview you with relay-relevant questions

## Done means
- A sourced comparison explains competitors' memory behavior and recommends what Relay should remember about its user.
- Globals provides a clearly named user-memory section where saved facts can be reviewed, edited and retired, with their runtime scope visible.
- An interview action starts the Globals helper with Relay-relevant questions and a way to save useful answers as user memory.
- Saved changes affect later agent context; unsaved drafts and external edits are protected.

## Plan
**Goal:** Make Relay's knowledge about its user understandable and controllable, informed by current competitor research.

**Findings:** `src/GlobalsPane.cpp` already edits global memory cards alongside aliases and instructions. `backend/relay_core/globals_protocol.py` persists them; `memories.py` loads them. `agent_context.py` and `RelayWindow.h` already host a Globals helper.

**Steps:**
1. Research official documentation for Warp, Claude Code, Codex, OpenCode and adjacent assistants; write a sourced design report.
2. Add a dedicated user-memory view to the existing Globals editor using the existing memory store.
3. Connect an interview entry point to the existing helper and teach it the memory workflow.
4. Run targeted backend/widget checks and isolated GUI verification; land with evidence.

**Risks:** Avoid a second conflicting profile store, accidental project-to-user promotion, silent inference of personal facts, and overwriting drafts or concurrently edited memory.

**Verify:** Memory/protocol/context tests, Globals widget tests, application build and isolated Xvfb interaction.

## Tests
- `tests/test_user_memory_tools.py` — save/get/edit/retire through the real store and next prompt; scope, duplicates, app-write setting and stale edits.
- `tests/test_globals_protocol.py` — existing editor persistence and conflicts.
- `tests/test_memories.py` — matching, precedence, supersession and budget.
- `tests/test_app_tools.py` — tool catalog, execution and settings.
- `tests/test_agent_context.py` — helper context contracts.
- `ctest -R ^globalspane$` — section filtering, summary search, draft protection, interview callback and existing editor behavior.
- manual: docs/qa_evidence/2026-09-22-user-memory/

125 backend tests pass using `PYTHONPATH=backend:tests python3 -m unittest -q test_user_memory_tools test_globals_protocol test_memories test_app_tools test_agent_context`. Pytest is not installed here; these are unittest tests. Widget test passes; application builds with scripts/relay-build. Local scripted-provider GUI drive verifies first-click interview submission, the Globals brief, actual app_user_memory save and editor refresh. Model interview quality remains for independent verification.

## Execution Summary
Completed a sourced comparison and recommendations in `reports/Relay user memory design.md`, supported by two research notes covering Warp, Claude Code, Codex, OpenCode, Cursor, VS Code and GitHub Copilot. Report distinguishes preview/historical features and proposed future work.

Globals now opens on User memory, separate from Aliases/Instructions/All records. User memories have readable summaries and content search; selected records remain editable in the existing Markdown editor with scope, pinning, override and retirement explanations. Drafts block section changes/interviews. Interview me opens the existing Globals helper, asks one relevant question at a time and proposes exact facts for confirmation. Explicit remember requests save directly. The new app_user_memory tool uses the same store, validates user scope and preserves editor concurrency guards; helper writes are attributed to agent.

A live drive found and fixed first-click ordering: the ask now follows console attachment. Retirement retains files/history; no automatic background inference, new use/generate toggle, true purge, or independently enforced per-fact consent token is claimed. Refresh shows helper saves. Evidence: `docs/qa_evidence/2026-09-22-user-memory/`.
