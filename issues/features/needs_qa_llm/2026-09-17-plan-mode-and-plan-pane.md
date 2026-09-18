---
id: THHF
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (GUI E1 subagent), 2026-09-17
rank: oy
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`; contract `docs/AGENT-SESSIONS-PROTOCOL.md`
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Plan mode: Shift+Tab, PLAN chip, editable plan pane with Execute

## Behavior as implemented

- Shift+Tab in the prompt box (`agent.planToggle`, prompt box only), /plan, or Actions › Plan mode sends `set_mode`; `mode_changed` shows or hides a PLAN chip in the prompt row and a toast.
- `plan_written` opens the plan in an editable pane beside the agent pane (an existing pane for the same file is reused and reloaded unless it has unsaved edits). The pane has Save / Reload, Ctrl+S, a "●" dirty marker in its title and the tab title, Markdown highlighting (KSyntaxHighlighting when available), and **Execute**, **Execute in fresh context**, **Keep planning**.
- Execute saves unsaved edits first, then sends `plan_execute {path, fresh}` (queued when the agent is busy; fresh requires an idle agent). The backend switches to build mode and runs the plan; fresh resets the conversation first.
- Keep planning returns focus to the agent pane's prompt box.
- Plans folder: Actions › Agent options › Plans folder… (absolute path; default `<workspace>/.relay/plans`).
- Layouts save plan panes as `{"plan": {"path": ...}}`.

## Implementer check (not a QA verdict)

Under Xvfb with Kimi K3 (`docs/qa_evidence/2026-09-17-plan-pane/`): Shift+Tab showed PLAN; "Plan how to add a subtract function…" produced `.relay/plans/2026-09-17-1022-add-subtract-function-with-test.md`, which opened beside the pane (`implementer-plan-written-pane.png`). Adding "- Also add a docstring to subtract." showed ● (`implementer-edited-dirty-marker.png`); Ctrl+S wrote it. Execute switched to build mode; the agent added `subtract` **with a docstring** (from the edit) and `test_calc.py`, and ran the tests (`implementer-executed-build-mode.png`).

Not verified live: Execute in fresh context, Keep planning, a custom plans folder.

## Limitations

- A plan pane restored from a closed tab has no owner pane; its Execute buttons do nothing.
- Closing a plan pane with unsaved edits does not ask.

## QA checklist

1. Shift+Tab, ask for a plan; check that no files change while planning.
2. Edit the plan, check ●, Ctrl+S, Execute; the agent follows the edited plan.
3. Plan again; Execute in fresh context: the conversation starts over and the plan still runs.
4. Keep planning; ask for changes; the same pane reloads with the new plan.
5. Set a plans folder in Agent options and plan again.
