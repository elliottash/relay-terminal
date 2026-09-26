# Verify EFT9 — the phone's reasoning-level chip (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). All seven linked commits are ancestors.

## Tests
- `RELAY_KEYRING=off python3 -m unittest tests.test_pane_view` — **one class errors, the rest pass**: OutboxTests.setUpClass fails on `tests/outbox_peer.mjs` importing named exports from `app/outbox.js` (module-format drift, third instance of #DX4A's family — noted on that card's thread). This card's chip tests pass under their later-renamed names:
  - `test_a_level_only_state_repaints_the_chip_and_the_closed_chip_has_no_tick` (was test_the_effort_chip_follows_a_state_that_only_moved_the_level) — **pass**
  - `test_a_model_whose_level_is_fixed_draws_a_chip_that_cannot_be_picked_from` (was test_a_fixed_level_draws_a_chip_that_cannot_be_changed) — **pass**
  - `test_the_effort_picker_shows_the_desktops_levels_and_sends_one` — **pass**
- `RELAY_KEYRING=off python3 -m unittest tests.test_remote_pane_state` — **OK** (the wire: whole-block effort validation, effort_fixed + reason end to end, refused-pick republish, guest capability walls).
- `ctest -R panestate` — **stale line**: no such test at HEAD; `relay-panestate` is now a static library (CMakeLists.txt:422), the C++ panestate coverage lives inside the suites above and relay-consolemode-tests.
- Implementer's three evidence dirs (phone-ux-drive, streamA-pane, streamE-effort) — **present**.

## Code at HEAD
- app/pane.js:1170 `renderEffort(m)` called from the top of `renderModel` (outside the model-signature guard); :311 a separate `rp-effort-chevron`; :1207 `effort_fixed` disables the chip. The three fixes from the card are all in the shipped source.

## Live
- No phone on this rig; the card's headless-Chrome measurements (chevron inside the model, level-only repaint, fixed-chip seam through the real hub cleaner) are the standing evidence and were re-run by the orchestrating session at landing.

## Verdict
PASS (chip and wire suites green at HEAD under their renamed tests; suite-wide blockers are #DX4A-family drift, not this card).
