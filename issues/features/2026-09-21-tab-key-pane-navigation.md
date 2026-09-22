---
id: PNAV
type: work
status: needs-verification
labels: [feature, keyboard]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex conversation, 2026-09-21'
links: {plans: [], commits: [72311d28bec6c603758d24ac0c737b3124a96cb6, b70fb8b4c3f76d0b2b741d3f67bc2faef9e988dc], evidence: [docs/qa_evidence/2026-09-21-pane-tab-navigation/], related: [], github: null}
---
# Tab and Shift+Tab navigate tabs inside eligible panes

## Issue
yes, lets go with this approach -- tab and shift+tab. i dont want the alt numbers, i will reserve that for something else later. this only works in tabbed panes where we dont need shift+tab for plan.

## Plan
Add a shared, explicitly registered pane-tab navigation helper and route eligible keys before the window action map. Register Projects/Sessions/Globals, Models, Options, standalone model picker and subagent tabs. Preserve console Plan/completion and writable editor keys. Remove Models Alt-number bindings, document the convention, and verify with targeted tests and an isolated live app.

## Done means
Tab goes forward and Shift+Tab backward with wraparound in eligible panes. Console Plan and editing keep their keys. Pane navigation adds no Alt-number bindings; window Ctrl+Tab remains unchanged.

## Tasks
- [x] Shared navigation helper and focused tests. <!-- t:ha -->
- [x] Integrate eligible panes, preserve console keys, remove Alt-number bindings. <!-- t:hb -->
- [x] Build, live keyboard verification and evidence. <!-- t:hc -->

## Tests
`ctest --test-dir build -R '^(panetabnavigation|modelspane|conversations)$' --output-on-failure`
passed. `docs/qa_evidence/2026-09-21-pane-tab-navigation/drive.py` passed against build
`2026-09-21.21H.06`; its captures cover both directions, wraparound, editor and console exclusions,
Models navigation, and reserved Alt-number keys.
