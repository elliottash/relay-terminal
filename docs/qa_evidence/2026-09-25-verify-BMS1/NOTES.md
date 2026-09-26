# Verify BMS1 — board model selection agrees with the card agent (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). Commit 86252f3e is an ancestor.

## Tests
- `PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest tests.test_card_model_selection tests.test_model_switch` — **OK** (the card's own suites).
- Adding `tests.test_board_protocol` as the card's Tests line did: 204→ now **2 failures**, both pre-existing scaffold drift unrelated to model selection — `InitTests.test_the_owners_first_card_asks_first_and_lands_on_a_yes` (files list expects RELAY.md, scaffold writes AGENTS.md; covered by existing card #42G1) and `WorkerWorkspaceTests.test_the_launch_directorys_board_is_never_adopted_by_a_workspaceless_configure` (same drift family; noted on #42G1's thread today). All model-selection cases in test_board_protocol pass.
- `scripts/relay-build --target relay` — **passed** earlier this sweep (HEAD-era tree).

## Code
- 86252f3e "Keep board model selections consistent with card providers (#BMS1)" is landed; its guards live in the worker/board ask path (`_usable_config` guest refusal per #GH5T, per-card config copies, idle-queue wait) and are exercised by the green `tests.test_card_model_selection`.

## Live
- Not driven live this pass: it requires live board-card agent turns against a real provider (the QA rig has no spendable keys). Covered by the implementer's evidence README + the green suites.

## Verdict
PASS (test evidence; live agent turns not exercised).
