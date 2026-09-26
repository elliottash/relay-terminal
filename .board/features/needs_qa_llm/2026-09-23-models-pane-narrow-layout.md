---
id: N4PW
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: codex
rank: m
created: '2026-09-23'
source: 'Codex in Relay, 2026-09-23'
links: {plans: [], commits: [e326182dbae13d6f8bedbb168cf195b9cfd32c92], evidence: [docs/qa_evidence/2026-09-23-models-narrow/, docs/qa_evidence/2026-09-25-verify-N4PW], related: [4BPE], github: null}
---
# Make every Models tab usable in a narrow pane

## Issue
can you examine each tab in the models pane and check that we can make it work well in a narrow pane

## Decisions
"available" doesnt need anything about effort / reasoning
"same with priorities, lets make effort a separate tab"

## Done means
At a narrow split width, Providers, Available, Priorities, and Jobs keep their primary labels and actions visible and usable without horizontal scrolling to a hidden action.
Available does not display an effort or reasoning control at any width.
Priorities does not display a reasoning control; a dedicated Effort tab edits the levels of ranked models.
Resizing between narrow and wide widths preserves selection and restores the wider layout.
The keyboard paths to each tab and its key actions continue to work.

## Plan
**Goal:** make Models useful in a narrow split, with effort settings on their own tab.

**Findings:** Providers uses SettingsPane's wrapping rows, but its long introduction pushes the controls below a narrow viewport. Available and Priorities share ModelPicker's fixed side lists and many table columns; Jobs uses four columns with fixed widths. Five top tabs need shorter narrow labels.

**Steps:** 1. Capture and measure each tab at a narrow width. 2. Adapt the picker and Jobs to width, preserving model names, availability, rank, current model, and actions. 3. Move reasoning controls from Available and Priorities to a dedicated Effort tab, and fit the tab strip. 4. Test narrow and wide geometry and capture all tabs.

**Risks:** hiding a column could hide an operation; retain keyboard access and surface any displaced action in a visible control or tooltip.

**Verify:** focused Qt widget tests, a Relay build, and isolated Xvfb captures at a narrow pane width.

## Execution Summary
The five Models tabs fit a narrow split. Providers starts with its controls visible; Available keeps model search and availability; Priorities keeps rank and box cutoff; the new Effort tab contains reasoning settings for ranked models; Jobs keeps the job and current model visible with full details and model selection below. The picker places its side controls below the list at narrow widths and restores the wide layout on resize. Available and Priorities have no reasoning controls. See `docs/qa_evidence/2026-09-23-models-narrow/` for widget and live app captures.

## Tests
`scripts/relay-build --target relay-modelspane-tests relay-modelpicker-tests relay-jobstab-tests relay` passed with the shared working tree.
`QT_QPA_PLATFORM=offscreen ./build/relay-modelspane-tests` — 23 passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-jobstab-tests` — 22 passed.
After the helper context update, `relay-modelspane-tests` passed again — 23 passed.
The current shared working tree's `relay-modelpicker-tests` passed 55 tests. The isolated commit source passed 23 Models pane tests and 49 Model picker tests against the committed baseline; two baseline tests were affected by another session's uncommitted catalog changes and were not included in this change.

## QA checklist
Verified 2026-09-25 by a verifying session at rev `540185020a18ecc15080849f99b03c9e8dcff9ea` (HEAD; commit `e326182d` is an ancestor). Evidence: `docs/qa_evidence/2026-09-25-verify-N4PW/` (screenshots + NOTES.md). Note: later cards renamed the tabs — Providers→Sources, Available→Enabled, Priorities→Pick order ("Order" when narrow), Jobs→Agent jobs ("Roles" when narrow); the Done means map onto those.

**Done means, item by item:**
- Narrow Providers/Available/Priorities/Jobs keep primary labels and actions visible, no hidden action behind horizontal scroll — **passed** (`10-narrow-sources.png` … `14-narrow-roles.png` at ~400 px pane width: search, add-provider, login/test buttons, on/off boxes, rank + Move up/down/Remove, job table all visible).
- Available shows no effort/reasoning control at any width — **passed** (`11-narrow-enabled.png`, and wide `01-models-wide.png`).
- Priorities has no reasoning control; a dedicated Effort tab edits ranked models' levels — **passed** (`12-narrow-order.png`, `13-narrow-effort.png`).
- Resizing narrow↔wide preserves selection and restores the wide layout — **passed** (`15-wide-restored.png`: Effort tab stayed selected, full tab labels restored).
- Keyboard paths to tabs and key actions keep working — **passed** (Ctrl+Shift+M opened the pane; Tab/Return walked the tabs, `16-keyboard-tab.png`).

**Tests, line by line:**
- `relay-modelpicker-tests` — **passed**: 62 passed, 0 failed (card recorded 55; coverage grew).
- `relay-jobstab-tests` — **passed with a note**: 23 passed, 1 failed; the failure (`tests/jobstab_test.cpp:181`, "system-pane agent" vs "helper agent") comes from later commit `e6febb9f` (#E8V1) renaming without updating the test, not from this change.
- `relay-modelspane-tests` — **passed with a note**: 25 passed, 1 failed, same #E8V1 cause (`tests/modelspane_test.cpp:782`).

Unresolved: nothing for this card. The two stale #E8V1 test expectations are filed separately. One observation, not a defect: the pane splitter did not respond to a synthetic xdotool drag, so narrowness was achieved by resizing the window; manual drag was not exercised.

Reviewed 2026-09-25 by the verifying session (qa-verify-N4PW), rev `54018502`.
