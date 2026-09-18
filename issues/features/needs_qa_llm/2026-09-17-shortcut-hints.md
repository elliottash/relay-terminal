---
id: B6E5
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: keyboard
assignee: implemented by Claude Opus 5 (GUI F1 worker), 2026-09-17
rank: vx
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Shortcut hints (Superhuman-style)

## Behavior as implemented

- `src/Hints.*`: on by default (`hints/enabled`), each hint at most 3 times, 10 min per-hint cooldown, 20 s global gap; counts in QSettings `hints/`. Text comes from the live keymap ("Next time: Ctrl+P · new pane to the right"); unbound actions show no key hint. 5 s toast in the pane.
- Triggers: toolbar and palette activation of actions with shortcuts; pane button row buttons; tab "+", tab close, tab ⧉; clicking into another pane of the same tab (Alt+arrows); mouse picks in the mode/model/effort pickers; clicking the directory line (@); queue × (Delete); `/shell `/`/agent ` (`!`/`*`); palette Rewind chat/code; pane drag (Ctrl+Alt+arrows); the first `✦ N tool calls` link; rotating idle tips 4 s after a finished agent turn with an empty prompt box (@ files, Shift+Tab plan, Esc Esc / /rewind-code, Down for agents, Ctrl+Shift+A, ! and *).
- Toggles: Actions › Agent options › Shortcut hints, Actions › Shortcuts › Shortcut hints / Reset shortcut hints. Rule for future features in WARP.md; documented in docs/ARCHITECTURE.md "Shortcut hints". Unit tests: `tests/hints_test.cpp` (ctest `hints`).

## Implementer check (not a QA verdict)

Xvfb, isolated config (`docs/qa_evidence/2026-09-17-shortcut-hints/`): split-right button → "Next time: Ctrl+P · new pane to the right" (`implementer-01`); toolbar Actions → "Next time: Ctrl+Shift+A · actions" (`implementer-02`); `/shell echo hi` → "Next time: type ! …" (`implementer-03`); idle tip after a finished turn (`implementer-04`).

## QA checklist

1. Click toolbar Actions three times over a minute: the hint appears at most once per 20 s and never a fourth time.
2. Rebind pane.splitRight in keybindings.json; the split button hint shows the new key.
3. Turn Shortcut hints off: no hints; Reset shortcut hints then on: hints return.
4. Mouse-pick a model and an effort; click the directory line; remove a queue item with ×.
5. After an agent turn, leave the prompt box empty for 5 s: one tip; typing cancels it.
