---
id: BXMS
type: work
status: needs-verification
labels: [feature, models]
assignee: agent
session: box-settings
rank: m
created: '2026-09-23'
source: 'Claude in Relay, 2026-09-23'
links: {plans: [], commits: [12ef73d794085128b594993acf9ee9cbc5e27839], evidence: [docs/qa_evidence/2026-09-23-box-all-models/], related: [N4PW, MDL1], github: null}
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

## Execution Summary
The model box ends with "all models" (`gear:all`) and "model settings" (`gear:picker`). "all models" reopens the box on `relay::modelrows::filtered` — every class whole plus every other available model, scrollable — and a pick there is an ordinary box pick; Right/Left do nothing in it and the next opening is the short list again. The Models pane no longer switches any pane's model: `ModelsPane::Target::use` is gone, the hosted `ModelPicker` hides "use" and Enter/double click only highlight, its footer and filter no longer promise "enter uses it", the header says the settings are for every pane, and the helper's screen line says "Opened from" rather than "Serving".

## Tests
On the exact tree landed (`land.py` verify): `relay-modelrows-tests` 23/23, `relay-modelspane-tests` 23/23 (new `enterPicksNoPanesModel`, `theHeaderSaysTheSettingsAreForEveryPane`), `relay-modelpicker-tests` 54/55. The one failure, `theFooterNamesTheRealKeys`, is failing at HEAD before this change. It came in with `e326182d` (#N4PW), which landed another session's footer split while their matching test edit is still uncommitted (`codex-model-ties`). Live: `docs/qa_evidence/2026-09-23-box-all-models/` (box, all models, a pick from it, model settings).

