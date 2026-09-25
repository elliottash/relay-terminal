---
id: FRVM
type: work
status: needs-verification
labels: [feature, options, security, agent-tools]
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: pane 1, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Agents may control Relay by default: flip the action allowlist to a refusal list

## Issue
i think by default agents, should be able to control relay -- options, actions, etc

## Execution Summary
Landed `abaf999d` (land.py build gate: the exact tree builds `relay`).

- `actionIsAgentSafe()` (src/AppCommands.cpp) is now allow-unless-named: refused are `refusedByTheOwner()` (voice.toggle, the control handoff keys, keybindings.clearOverrides, history.clear) and `pane.close` (closes the focused pane, not an aimable one). Options buttons: `rowButtonIsAgentSafe()` — all but `remote.pair` (#W5N2: a person admits a device).
- The old table survives as `actionIsAudited()`. Audited actions run inline; unaudited ones run via `QTimer::singleShot(0)` after the answer is sent (`deferred: true`), so a handler that opens a modal dialog cannot hang §30.3's deadline or freeze the window. This let `windows.fresh` on.
- GUI catalog lists refused registry keys as `agent_safe: false`; the worker (`app_tools.py`) marks every other shortcut-only registry key runnable and resolves it for `app_action_run` (`_runnable_action`). Tool/prompt text and protocol §30.2 amended.
- Not touched: `docs/ARCHITECTURE.md:2004` still says "opt-in" — the file is claimed by live land session 234z; a one-line follow-up once it lands.

Tests: `ctest -R '^appcommands$'` 60/60 pass (new: `agentSafeIsOnUnlessRefused`, `anUnauditedActionRunsAfterTheAnswer`, button refusal of `remote.pair`); `python3 -m unittest tests.test_app_tools` 84 OK (shortcut-only key now runs); agent_context tests 24 OK.

To verify by hand: ask a pane agent to run an action that was off before (e.g. `windows.fresh` or an Options "delete" button) — it answers at once and the dialog appears for you; `history.clear` is refused.
