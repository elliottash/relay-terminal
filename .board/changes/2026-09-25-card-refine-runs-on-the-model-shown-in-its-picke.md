---
id: ZPSG
type: work
status: needs-verification
labels: [bug, models, switchboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 97e9802a-be38-415b-b81d-ddcb07e5cd62
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'Select a model in a card pane, press Refine, and confirm the response and provenance match that selection.', sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [da8d8c1b43a0, fdb1e0fa5880], evidence: [tests/test_card_model_selection.py], related: [BMS1, E34S], github: null}
---
# Card Refine runs on the model shown in its picker

## Issue
A card pane Refine turn can run on a different model than the one shown in its picker. Make the selected model govern the card turn and report a clear refusal if it cannot run.

> bug: "refine" didnt use the model i had in the picker, in a card pane
> — elliott · [session:6fdd14d97d384fdf8c839e64e9916e0c](relay://session/6fdd14d97d384fdf8c839e64e9916e0c) · 2026-09-25

## Done means
A Refine turn in a card pane runs on the model displayed in that pane's picker.
The recorded turn provenance names the model that actually served the turn.
If the selected model cannot run, the card says so before any model call; it does not silently use another model.

## Plan
Goal: make the card pane's model choice govern Refine and other card turns.
Findings: the card console uses the tab helper's model; `_console_model` silently substitutes a priority-list provider whenever that model is a guest. Existing native model synchronization is covered by #BMS1.
Steps:
1. Give each card console a guest provider when its selected helper model is a guest, including cached conversations after a model change.
2. Close guest providers when card sessions are forgotten or evicted.
3. Add focused tests for guest selection, cached switching, and Refine provenance.
Risks: guest session startup can fail; return that failure to the card without falling back to another model. Multiple open cards may have separate guest processes.
Verify: focused card model selection and board protocol tests on the exact landing tree.

## Tests
- `tests/test_card_model_selection.py`
- `tests/test_board_protocol.py::AskTests`
- `tests/test_board_protocol.py::ModeTests`

### Check
`PYTHONPATH=backend:tests RELAY_KEYRING=off python3.12 -m unittest tests.test_card_model_selection tests.test_board_protocol.AskTests tests.test_board_protocol.ModeTests` passed: 44 tests on landed commit `da8d8c1b43a0`. A scripted guest Refine reaches the selected guest and writes its model in the card thread. Cached switches and guest cleanup pass.

Broader `tests.test_board_protocol` has one pre-existing scaffold assertion failure (`RELAY.md` versus `AGENTS.md`, tracked on #42G1). Broader `tests.test_guest_harness_provider tests.test_roles` has one pre-existing account-list fixture failure with this profile's registered guest accounts.

### Check 2026-09-25 21:30
- not-applicable · unittest:tests.test_card_model_selection — tests/test_card_model_selection.py is not in the project any more
- passed · unittest:tests.test_board_protocol.AskTests — tests/test_board_protocol.py::AskTests passed for this revision on spark-dcc9, 2026-09-26T01:29:32Z
- passed · unittest:tests.test_board_protocol.ModeTests — tests/test_board_protocol.py::ModeTests passed for this revision on spark-dcc9, 2026-09-26T01:29:32Z
- notice · unittest:tests.test_card_model_selection — tests/test_card_model_selection.py: 12 of 12 are not in the project any more (test_a_cached_card_leaves_its_guest_when_the_picker_changes_to_native, test_a_cached_card_moves_to_the_guest_and_keeps_history, test_a_card_console_takes_the_selected_guest_instead_of_the_priority_list…)
history: thread

## Execution Summary
Card conversations now launch an independent guest harness when the card pane's selected model is a guest. Cached conversations switch onto that guest without losing history, reuse it on later turns, and close it when the card session ends. Refine's answer records the model that served it. Landed as `da8d8c1b43a0` and `fdb1e0fa5880`. The latter makes the test collect under the standard runner. The focused suite passed 44 tests at `fdb1e0fa5880`.
