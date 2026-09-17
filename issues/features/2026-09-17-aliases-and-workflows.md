---
id: G8DK
type: work
status: ready
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
rank: 1q
created: '2026-09-17'
acceptance: a saved command or prompt with parameters can be defined globally or per project, found in the palette and run by name
source: '`issues/feature_intake.txt`, 2026-09-17: "add a good version of aliased terminal commands / prompts that can be added globally or locally (sort of like warp \"workflows\")"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Aliased terminal commands and prompts (Warp workflows)

## Notes
Owner decision (TASKS-AND-MEMORY-DESIGN.md section 9): global aliases live in the global Switchboard, local ones in the repo Switchboard.

## Open questions
1. One format for both commands and prompts (Markdown with front matter, `{{arg}}` parameters with defaults)?
2. Invocation: palette, `/name` for prompts, and a name typed in terminal mode?
3. Import existing Warp workflows and shell aliases?
4. Can the agent create aliases from repeated commands (proposed, logged)?

## Decisions (owner, 2026-09-17)
All recommendations accepted: one Markdown file per alias (command or prompt) with `{{arg}}` parameters and defaults,
global in the global Switchboard and local in the repo Switchboard; run from the palette, `/name` and the name in
terminal mode, filling parameters in the composer with Tab; import Warp workflows and shell aliases with a preview;
the agent may suggest aliases for repeated commands (suggestion only, logged).
