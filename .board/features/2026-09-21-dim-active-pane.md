---
id: D8AP
type: work
status: needs-verification
labels: [feature, panes, appearance]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: zzzzzzzzzzzzzzzzx
created: '2026-09-21'
source: User request in Relay, 2026-09-21
links: {plans: [], commits: [c9b00e167d9a19530d860acffb384500e2cfd885], evidence: [docs/qa_evidence/2026-09-21-dim-active-pane/], related: [RG0Z], github: null}
---
# Include the active pane in dim while working

## Issue
dim while working currently does not apply to the pane you are working in. add a suboption, off by default, that applies it also to your active pane. 

## Plan
Goal: opt-in automatic dimming of the active pane.

Findings: src/PaneDimming.h owns reveal precedence; src/RelayWindow.h owns Appearance options and polling. SettingRow already supports nested rows.

Steps: add Include active pane under Dim while working; use its default-false persisted setting in the policy; test active/inactive transitions, live toggling, attention and manual overrides; build and verify under isolated Xvfb.

Risks: preserve manual reveal and attention precedence; include-active has no effect while automatic dimming is off. Keep edits separate from concurrent Models work.

Verify: targeted panedimming test, app build, isolated GUI check, board validation.

## Execution Summary
Added Options > Appearance > Dim while working > Include active pane, indented and off by default. Its persisted setting applies automatic dimming to the selected busy pane, including after re-entry; questions, blocked states and completion still reveal it. Manual dimming and explicit brightness overrides retain their precedence.

## Tests
`ctest:panedimming`

`manual: docs/qa_evidence/2026-09-21-dim-active-pane/`

Executed: ctest --test-dir build -R '^panedimming$' --output-on-failure (passed). Exact app tree builds; isolated Xvfb option visibility, default-off checkbox, click and persisted value checks pass.

## QA checklist
- [ ] In Appearance, verify Include active pane is nested under Dim while working and defaults off.
- [ ] Enable both options: the active working agent dims, including after leaving and re-entering it.
- [ ] Toggle Include active pane off: selected pane becomes bright while inactive busy panes remain dim.
- [ ] Questions, blocked states and completion reveal automatically dimmed panes; manual overrides remain effective.
- [ ] Restart and confirm persistence; reset Appearance restores the option to off.
