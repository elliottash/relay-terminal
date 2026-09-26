---
id: BXMS
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: agent
session: box-settings
rank: m
created: '2026-09-23'
source: 'Claude in Relay, 2026-09-23'
links: {plans: [], commits: [12ef73d794085128b594993acf9ee9cbc5e27839], evidence: [docs/qa_evidence/2026-09-23-box-all-models/, docs/qa_evidence/2026-09-25-verify-BXMS/], related: [N4PW, MDL1], github: null}
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

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean-worktree builds). Evidence: `docs/qa_evidence/2026-09-25-verify-BXMS/`.

**Done means, item by item:**
- Models pane edits settings only; Enter/double-click/"use" switch no pane's model; header has no "for: <pane>" — **passed**: `enterPicksNoPanesModel` + `theHeaderSaysTheSettingsAreForEveryPane` green (4/0); header live reads "Shared model settings - choose an individual pane's active model in its model box" (`02-all-models-list.png`); `src/ModelsPane.cpp:255` has no `onUse`.
- Box ends with "all models" and "model settings"; "all models" reopens the full scrollable list and a pick there is an ordinary box pick; "model settings" opens the Models pane — **passed with a note**: rows live (`01-box.png`), full list live (`02-all-models-list.png`: role rows + "other models" incl. gpt-6-*); pick-as-box-pick is covered by the green `modelrows`/`modelpicker` suites — the live pick switch itself was not cleanly captured (the fresh-profile "instruction files" overlay steals focus mid-drive); "model settings" opens the pane (it opened during the drive, though via my own stray click — not counted as clean evidence, code + suites cover it).

**Tests, line by line:**
- `relay-modelrows-tests` — **passed** (ctest modelrows green).
- `relay-modelspane-tests` — **passed with a note**: 25/1, the 1 being the #E8V1 stale-string failure (#SYTR) in a test this card doesn't own; this card's own named tests pass.
- `relay-modelpicker-tests` — **passed**: 62/0 (card recorded 54/55 with `theFooterNamesTheRealKeys` failing — that one is fixed since).
- Live capture `docs/qa_evidence/2026-09-23-box-all-models/` — **present**.

Unresolved: nothing material; the one live nicety (pick-from-all-models switch captured cleanly) is test-covered.

Reviewed 2026-09-25 by the verifying session (qa-verify-BXMS), rev `2db96643`.
