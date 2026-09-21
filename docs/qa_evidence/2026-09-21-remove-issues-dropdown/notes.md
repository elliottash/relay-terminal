# Prompt issues dropdown removal — #DRP7

- `scripts/relay-build --target relay`: passed (build 2026-09-21.17H.14).
- `ctest --test-dir build -R '^(requests|subagents)$' --output-on-failure`: 2/2 passed.
- Live GUI under Xvfb with isolated XDG directories and a fixture worker (no model calls): prompt has no work/issue button; Shift+Tab shows PLAN at the left; Ctrl+Shift+K opens Tasks.
- Screenshots: `01-prompt.png`, `02-plan.png`, `03-tasks.png`.
- Source review: task callback still refreshes RequestsPanel and SubagentsPanel; current-card header remains; plan_written still prints the path and calls onPlanWritten. Card navigation and plan file reopening remain on the verifier checklist.
- Board format check: no findings for this card/thread. Repository-wide check reports pre-existing findings on other cards.
