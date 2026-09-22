---
id: BMS1
type: work
status: needs-verification
labels: [bug, models, switchboard]
assignee: codex
rank: m
created: '2026-09-22'
source: 'Owner, Relay terminal conversation'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-board-model-selection/], related: [CTRN, MDL1, GH5T], github: null}
---
# Board model selection agrees with the card agent

## Issue
check out this inconsitency: @/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260921-194033.png

this is the board agent. the model selected was astra, but you can see at the top it says kimi

## Decisions
"fix that+"

## Done means
A reused card answers on the selected supported provider, retaining its conversation.
An unsupported guest selection is refused explicitly and restores the actual model in the picker.
A selection cannot change the picker underneath a running or queued card turn.
Terminal-pane model switching remains unchanged.

## Plan
Goal: keep the tab console selection and per-card provider consistent.
Findings: worker model commands address the main agent; cached card agents retain old configs. Guest helpers are explicitly unsupported by `_usable_config` (#GH5T).
Steps: guard unsupported/busy helper selections; synchronize idle card providers before an ask; test real agents and refusal events; build the picker refusal text change.
Risks: preserve the existing guest-helper restriction and card Plan permissions; do not enable guest execution as a side effect.
Verify: regression tests for reused cards, guest refusal, busy queues and terminal isolation; targeted model-switch and board protocol tests.

## Execution Summary
Cached idle card agents adopt the helper's selected native provider before their next turn, retaining their conversation. Configs are copied so per-card reasoning changes cannot mutate the tab's template. Guest model/role selections are refused before launching a harness, and model changes wait until the card queues are idle. Refusal restores the actual picker model and explains the board limitation instead of claiming the context window is too small.

## Tests
`python3 -m unittest tests.test_card_model_selection tests.test_board_protocol tests.test_model_switch` — 204 passed with PYTHONPATH=backend:tests and RELAY_KEYRING=off.
`scripts/relay-build --target relay` — passed.
Evidence: docs/qa_evidence/2026-09-22-board-model-selection/README.md
