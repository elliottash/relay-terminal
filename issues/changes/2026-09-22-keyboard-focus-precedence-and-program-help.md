---
id: KYPR
type: work
status: inbox
labels: [bug, keyboard, focus]
rank: m
created: '2026-09-22'
source: 'Owner in a Codex Relay pane, 2026-09-22; keyboard-system survey'
links: {plans: [], commits: [], evidence: [], related: [RBVK, D60R, T9ZS, ACDG], github: null}
---
# Resolve global versus local shortcuts and preserve program help

## Issue
this is great. we shoudl definitely fix the identified gaps so card those first.

## Planning notes
Read-only source audit at c2a4b370, 2026-09-22; GUI reproduction is still needed:
- `src/RelayWindow.h::eventFilter()` matches global Keymap actions before widget event filters, with specific exceptions for composer-only actions and ModelPicker Alt+Up/Down.
- `src/SettingsPane.cpp::eventFilter()` assigns Ctrl+N to next result, while the Relay keymap assigns Ctrl+N to new window. There is no corresponding global-dispatch exception.
- `src/FilePanes.cpp` assigns Alt+Up to the parent folder; `src/Keymap.h` assigns Alt+Up to focus the pane above. The global exception currently names ModelPicker, not Explorer.
- `src/Keymap.h::actsInsidePrograms()` allows F-keys under the default shift-only policy, and `help.shortcuts` includes F1. This takes F1 for Relay Actions while a terminal program has focus, contrary to the stated program-help rationale in `docs/KEYBINDING-PRESETS.md`.
- The collision list checks the global lookup table, not competing local widget handlers. Define and exercise scope/precedence rather than treating a globally unique chord as sufficient evidence.

## Decisions
Owner: "i dont want a ctrl and ctrl shift to have different funcs". Apply this to the proposed Relay shortcut system; clarify the treatment of conventional editor operations and terminal-program pass-through during design.

## Done means
Each tested shortcut performs the documented command in its focus context, exactly once. Editor and program input remains usable, program F1 is not taken by Relay's command finder, and local/global conflicts have explicit precedence and regression coverage.

## Tests
Planned: focused GUI regressions for Actions search Ctrl+N, Explorer Alt+Up, F1 in a terminal program, and matching Ctrl/Ctrl+Shift command families; relevant `tests/settingspane_test.cpp`, `tests/filepanes_test.cpp`, `tests/panetabnavigation_test.cpp`, and `tests/test_keybindings.py`.
