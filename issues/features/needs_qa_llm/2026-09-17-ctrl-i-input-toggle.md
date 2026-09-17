# Ctrl+I toggles terminal command vs agent prompt

- **Status**: needs-qa-llm
- **Component**: gui
- **Workstream**: routing
- **Milestone**: desktop-alpha
- **Assignee**: implemented by Claude Opus 5 (Claude Code session, pane UX subagent), 2026-09-17
- **Acceptance evidence**: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
- **Source**: `issues/feature_intake.txt` ("i think ctrl+I should toggle terminal command vs agent prompt")

## Behavior as implemented

- New action `input.toggle`, default Ctrl+I, acting only when focus is in the pane's prompt box (like Ctrl+H). In the terminal Ctrl+I stays Tab.
- Agent → Terminal; Terminal or Auto → Agent. Toast "Input: Agent" / "Input: Terminal"; the pane's mode picker follows.
- Palette: Agent › Toggle terminal / agent input. README shortcut table updated.
- Warp preset: Ctrl+I now maps to `input.toggle` (Warp's own Ctrl+I is a toggle); its `input.modeAgent` binding was dropped. `docs/KEYBINDING-PRESETS.md` updated.

## Implementer check (not a QA verdict)

Xvfb: Ctrl+I twice in the prompt box showed "Input: Agent" then "Input: Terminal" with the picker updating; in native terminal input, `ls uniquef` + Ctrl+I completed to `uniquefile_abc.txt`.
Evidence: `docs/qa_evidence/2026-09-17-ctrl-i-input-toggle/`.

## QA checklist

1. Toggle from Auto goes to Agent; the next submit routes accordingly.
2. Inside vim (human control) Ctrl+I reaches vim (jump forward).
3. Each preset: no conflict reported in the status bar.
