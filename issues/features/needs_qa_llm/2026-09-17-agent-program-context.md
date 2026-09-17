---
id: W011
type: work
status: needs-qa-llm
component: [gui, worker, agent]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code session, pane UX subagent), 2026-09-17
rank: bm
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: owner report 2026-09-17 (agent ran unrelated echo animations instead of typing into vim); owner approved the fix
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Tell the agent which program owns the terminal

## Behavior as implemented

- When a prompt is submitted while a foreground program runs, the GUI reads its command line from `/proc/<tpgid>/cmdline` (fallback: KonsolePart's foreground process id) and sends `"context": {"foreground_program": "vim scratch.txt", "terminal_cwd": "..."}` on the worker `ask` message.
- The backend validates the fields (known keys only, size limits, control characters stripped) and prepends a note to that user turn, delimited by "[Relay context: added by Relay, not typed by the user]" … "[End of Relay context]". The note says the agent cannot see or type into the program and that run_command runs in a separate background shell.
- Code: `validate_context` / `format_context` in `backend/relay_core/agent.py`; carried through `TurnSupervisor.submit`.

## Implementer check (not a QA verdict)

Xvfb, Kimi K3, vim running: the reply began "I can't type into vim myself — my commands run in a separate shell, not in your terminal pane" and suggested keystrokes instead of running commands.
Evidence: `docs/qa_evidence/2026-09-17-agent-program-context/implementer-agent-says-cannot-type-into-vim.png`. Tests: `ContextTests` in `tests/test_agent.py` (5).

## QA checklist

1. Repeat with GLM-5.3 and DeepSeek; none should run commands pretending to type into the program.
2. `python3` REPL and `less`: the context names the program correctly.
3. At an idle prompt no context is sent (agent behaves as before).
