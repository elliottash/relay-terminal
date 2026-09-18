---
id: X2N2
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: composer
assignee: implemented by Claude Opus 5 (GUI F1 worker), 2026-09-17
rank: pj
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# ! and * prompt prefixes (Claude Code style)

## Behavior as implemented

- Typing `!` as the first character of an empty prompt box (no Ctrl/Alt/Meta) is consumed and switches to Terminal mode with an amber "! terminal" chip; `*` switches to Agent mode with a cyan "* agent" chip. Backspace on the empty box restores the previous mode. The prefix applies to one submission, then the previous mode returns. Pasting "!…"/"*…" does not switch. `/shell ` and `/agent ` still work and show a hint.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-prefix-modes/`: `*` then `ls` → chip, mode Agent, label "AGENT · Explicit agent destination" (`implementer-01`); Backspace ×3 → Auto detect, no chip (`implementer-02`); `!` then "show me the files" → Terminal chip, "command not found: show · the agent will fix it" (`implementer-03`); pasting "*pasted" → text kept, mode Auto (`implementer-04`).

## QA checklist

1. `!` then `git status` Enter: runs in the terminal; the mode picker returns to its previous value.
2. `*` then `ls` Enter: goes to the agent.
3. Type `a!`: no switch. Paste `!ls`: no switch.
4. `*`, Backspace: back to the previous mode, chip gone.
5. IME input starting with `!` or `*` (if available) does not misfire.
