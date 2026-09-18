---
id: QRNJ
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (GUI E1 subagent), 2026-09-17
rank: dx
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`; contract `docs/AGENT-SESSIONS-PROTOCOL.md`
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# AI next-command and next-prompt suggestions

## Behavior as implemented

- Both are off by default: Actions › Agent options › AI next-command suggestions / Suggested next prompts.
- **Next command:** after a command submitted from the prompt box finishes, Relay sends `suggest {kind:"next_command", command, exit_status, cwd}`. If the prompt box is still empty when the `suggestion` arrives, the command shows as dim ghost text in place of the placeholder, with the tooltip "Suggested command (AI) · → or Tab accepts". →, Ctrl+F or Tab accepts; typing anything dismisses it.
- **Next prompt:** after an agent turn completes with nothing queued, Relay sends `suggest {kind:"next_prompt"}`; the suggestion shows the same way ("Suggested prompt (AI) · Tab accepts"). It is not shown when the input mode is terminal; the backend returns nothing in plan mode.
- Stale suggestions (a newer request, or text typed meanwhile) are ignored.

## Implementer check (not a QA verdict)

Under Xvfb with Kimi K3 (`docs/qa_evidence/2026-09-17-agent-suggestions/`): after "What does calc.py contain?" the ghost "Add subtract, multiply, and divide functions to calc.py" appeared and Tab filled it in (`implementer-next-prompt-ghost-tab-accept.png`). After `python3 -c 'import calc; print(calc.add(2, 3))'` the ghost `python3 -m pytest -q 2>/dev/null || ls` appeared with the AI tooltip, and → accepted it (`implementer-next-command-ghost-tooltip-right-accept.png`).

## Limitations

- Commands typed directly in the terminal (not through the prompt box) do not trigger a suggestion.
- The command's output tail is not sent (`output_tail` is optional in the protocol); suggestions rely on the command and exit status.

## QA checklist

1. With both settings off, nothing is requested (no extra latency or tokens).
2. Turn on next-command; run a failing command; check the suggestion fixes or follows up; →, Tab, Ctrl+F accept; typing dismisses.
3. Turn on next prompts; finish a turn; Tab accepts; Enter on the empty box still does nothing.
4. Plan mode: no next-prompt suggestion.
