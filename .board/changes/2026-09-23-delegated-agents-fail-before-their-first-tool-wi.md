---
id: D09N
type: work
status: needs-verification
labels: [bug, subagents, backend]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: ae07bf4f-9697-448a-915e-30847f87be93
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Codex in Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [40SN, KHY0], github: null}
---
# Delegated agents fail before their first tool with AttributeError

## Issue
implement the cards, and you can collect other signal-generated cards and start crunching through them with subagents (you can use glm 5.3 flash for the subagents)

## Planning notes
Six Relay `agent` delegates (a1–a6) launched from the 2026-09-23 implementation turn ended with `Agent error (AttributeError).` before any tool call. The failures include explicit `glm-5.3-flash`, role `flash`, and the default model, so they are not explained by one model spelling. `worker.log` records native child `turn_end outcome=error ms=0 tools=0` for multiple child sessions. The delegate tool returns no traceback; `backend/relay_core/agent.py` redacts unexpected turn exceptions to their class. Fresh `PYTHONPATH=backend:tests python3 -m unittest test_subagents -q` passed 37 stub-provider tests, leaving the live-provider/native-turn boundary untested. No child changed code or cards.

## Done means
A delegated agent on the configured native provider can start, call a harmless read-only tool, and return a result. An unexpected child exception retains a private diagnostic traceback tied to its run/turn without exposing prompts or credentials in the pane; the user-facing failure is actionable. Default and Flash selections both work or report a specific provider/configuration refusal.

## Plan
**Goal:** restore native delegated turns and make the next unexpected failure diagnosable.

**Findings:** `backend/relay_core/agent.py` catches unexpected turn exceptions and presents only `Agent error (AttributeError).`; six real child attempts failed at zero tools. `tests/test_subagents.py` passes 37 stub-provider tests, so the failing path needs a live-shaped integration test.

**Steps:** 1. Reproduce with an isolated worker and a deterministic provider that exercises child setup and one model request; capture the exact traceback privately at the exception boundary, with bounded non-sensitive metadata in diagnostics. 2. Fix the demonstrated missing attribute or construction mismatch in the child path, preserving model-role selection and parent isolation. 3. Verify default and Flash delegate calls from an isolated pane; run focused subagent/provider tests.

**Risks:** The shared backend is being edited for guest accounts. Do not alter unrelated provider/account code or copy prompts into diagnostic logs. Avoid retrying paid providers until the deterministic reproduction narrows the failure.

**Verify:** New failing-then-passing child integration regression, `tests/test_subagents.py`, and one isolated Relay delegate that reads a harmless file and returns.
