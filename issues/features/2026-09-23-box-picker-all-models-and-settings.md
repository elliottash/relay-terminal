---
id: BXMS
type: work
status: executing
labels: [feature, models]
assignee: agent
session: box-settings
rank: m
created: '2026-09-23'
source: 'Claude in Relay, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [N4PW, MDL1], github: null}
---
# Pick a pane's model only from its box; the box offers "all models" and "model settings"

## Issue
i also think, the models pane should not select models for specific panes. that should be done with the box picker. 

in the box picker, it should have:
all models -> opens the full scrollable list
model settings -> opens the models pane

## Done means
The Models pane edits settings only: Enter, a double click and the old "use" button no longer switch any pane's model, and its header no longer says "for: <pane>".
The model box (Alt+M) ends with two rows, "all models" and "model settings". "all models" reopens the box on every class's full list plus every other available model, scrollable, and picking a row there switches this pane as any box row does. "model settings" opens the Models pane.
Failure shows as a Models pane Enter that changes a pane's model, or a box without those two rows.

## Plan
1. `relay::modelrows::box` ends with "all models" (`gear:all`) and "model settings" (`gear:picker`); the full list (`filtered`) leaves "all models" out.
2. `Pane::modelBoxPicked` answers `gear:all` by reopening the box on the full list; Right/Left do nothing there.
3. `ModelsPane` stops wiring `ModelPicker::onUse`; `ModelPicker` hides "use" and ignores Enter/double click when nothing listens. The header says the settings are for every pane and that a pane's model is picked in its box. `RelayWindow` stops passing `Target::use`.
4. Tests: modelrows, filterpopup, modelspane, modelpicker.
