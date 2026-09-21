---
id: SHCK
type: work
status: done
labels: [bug, remote]
component: [gui]
rank: m
created: '2026-09-21'
source: 'owner, 2026-09-21, Claude Code session'
links: {plans: [], commits: [], evidence: [], related: [W5N2, SWPH], github: null}
---
# Clicking "Guest prompts run immediately" on the Sharing pane crashed Relay

## Issue

when i clicked the "guest prompts run immediately" checkbox, it crashed relay

## Execution Summary

`~/.local/share/relay/logs/relay.log` at 2026-09-21T23:37:00Z: `gui_crash signal=11 SIGSEGV` with
the frames `QCheckBox::nextCheckState → QAbstractButton::setChecked →
QAccessible::updateAccessibility → QAccessible::queryAccessibleInterface`. The box's `toggled`
reached `SharingView::onOptions`, the window wrote the option to the sharing model, the model's
change came straight back as `SharingView::refresh()`, and `build()` rebuilt the pane wholesale
with `delete widget` on every old row, including the checkbox that was still inside
`setChecked`. Qt then touched the freed box for accessibility. The same path is behind the second
checkbox ("Guests can act only while I'm here").

Fix (`src/SharingPane.cpp`, `SharingView::build`): old rows leave the layout and the screen at
once but are freed with `deleteLater`, hidden and parentless in the meantime, so nothing can click
them. `tests/sharingpane_test.cpp` reproduces the window's wiring (option written to the model,
view refreshed synchronously) and asserts the clicked box is still alive when its click returns,
gone after the deferred delete, and replaced by a checked one.

## Tests

- `ctest --test-dir build -R '^sharing$'`
