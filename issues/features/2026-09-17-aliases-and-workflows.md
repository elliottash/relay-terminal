# Aliased terminal commands and prompts (Warp workflows)

- **Status**: open
- **Component**: gui, worker
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: a saved command or prompt with parameters can be defined globally or per project, found in the palette and run by name
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "add a good version of aliased terminal commands / prompts that can be added globally or locally (sort of like warp \"workflows\")"

## Notes
Owner decision (TASKS-AND-MEMORY-DESIGN.md section 9): global aliases live in the global Switchboard, local ones in the repo Switchboard.

## Open questions
1. One format for both commands and prompts (Markdown with front matter, `{{arg}}` parameters with defaults)?
2. Invocation: palette, `/name` for prompts, and a name typed in terminal mode?
3. Import existing Warp workflows and shell aliases?
4. Can the agent create aliases from repeated commands (proposed, logged)?
