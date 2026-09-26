# Verify N4PW — narrow Models pane (2026-09-25)
Revision checked: 540185020a18ecc15080849f99b03c9e8dcff9ea (HEAD). Commit e326182d is an ancestor.

## Tests (xvfb-run, build at HEAD via scripts/relay-build)
- relay-modelpicker-tests: 62 passed, 0 failed.
- relay-jobstab-tests: 23 passed, 1 failed — failure at tests/jobstab_test.cpp:181 expects ["agent turns","subagents","helper agent"], actual "system-pane agent": caused by later commit e6febb9f (#E8V1) renaming without updating the test. Not this card's change.
- relay-modelspane-tests: 25 passed, 1 failed (same #E8V1 label cause, tests/modelspane_test.cpp:782).

## Live drive (Xvfb :97, isolated XDG_CONFIG_HOME/RELAY_WORKSPACE in this dir, window resized 1600→860 px so the Models pane is ~400 px)
- 00-main.png, 01-models-wide.png: wide baseline (tabs: Sources Enabled Pick order Effort Agent jobs — renamed since this card by later cards).
- 02-04: splitter drag attempts (no-op; splitter not draggable by synthetic drag — window resize used instead).
- 05-narrow-window.png: narrow labels kick in (Pick order→Order, Agent jobs→Roles).
- 10-narrow-sources.png … 14-narrow-roles.png: every tab usable at narrow width — primary labels and actions visible, no horizontal scroll. Enabled has no effort/reasoning control; Order has no reasoning control; Effort tab holds the per-model level controls.
- 15-wide-restored.png: resize back to 1600 px restores wide labels and preserves the selected tab (Effort still active).
- 16-keyboard-tab.png: Tab/Return keyboard path switches pane tabs (Effort → Agent jobs).
