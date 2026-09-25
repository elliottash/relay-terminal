---
id: 0CJY
type: work
status: done
labels: [bug, agent]
assignee: agent
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: none, criteria: 'pytest: pane_send labels build without raising at start and result time; the outcome note rides the result title; a raising started_label cannot end a turn; every ToolExecutor tool name labels cleanly', sign_off: none, effort: low, stakes: rework}
links: {commits: [d2a60b55a4d3c2132f4ef011e0e44f1850fcb266], related: [R5TC, G9VE, TK9C, D09N]}
---
# pane_send's label NameErrors before the tool runs; agent turns die with no failover

## Issue
check here: 6dbee2a7

there was an agent error, and the fallback to same-class models didnt work
Pane `6dbee2a7` (agent session `d68fca68…`) lost two turns to `NameError: name 'result' is not defined` (2026-09-25 00:21:42 and 00:24:11, turns `62ebf4df…`/`2617ef70…`), each ending `outcome=error retries=0`.

Root cause: `d33c1d6d` (#R5TC) put the `pane_send` label's outcome note in `_base` — but `_base(name, args, existed)` has no `result` parameter and neither `started_label` nor `result_label` passes one. Every `pane_send` call raised at `agent.py`'s `tool_started` emit, *before* the tool executed. The worker log holds zero successful `tool=pane_send` calls: the tool has been dead on arrival since it landed.

The "fallback to same-class models didn't work" half is by design, not a bug: failover (#G9VE) arms only on `ProviderError` from `_model_call` — the provider answered; Relay's own label code crashed. Switching glm-5.3 → opus would replay the same `pane_send` call into the same NameError. The restart advice was also wrong: the on-disk code was broken (`source_changed()` is true in this checkout most of the day, so #D09N's message misattributed a plain bug to module mixing).

Done means: `pane_send` labels build at started- and result-time, the outcome note lands where `result` actually exists, a label bug can no longer end a turn (it degrades to the plain tool name), and a sweep over every tool name keeps the whole registry label-safe.

Also hardens: `agent.py` wraps all five `tool_labels.*` call sites in a `_safe_label` guard.

## Execution Summary
- `backend/relay_core/tool_labels.py`: the outcome note moved out of `_base` (which has no `result`) into `result_label`, appended to the title only when the send happened.
- `backend/relay_core/agent.py`: new `_safe_label()` wraps all five `tool_labels.started_label`/`result_label` call sites; a label that raises degrades to `{kind: tool, running, title: <name>}` (with an honest `ok` on the result side) and logs `label_error` instead of ending the turn.
- `tests/test_tool_labels.py`: `PaneSendLabelTests` (started/result/refused) and `EveryToolLabels`, a sweep that builds a `ToolExecutor` with a roster and labels every offered tool at start and result — the hole that let #R5TC ship.
- `tests/test_agent.py`: `test_a_label_bug_cannot_end_the_turn` patches both label builders to raise and asserts the call runs, the ✓/✗ stays honest and the turn reaches `turn_summary`.

Landed as `d2a60b55` on main (contested-hunk review passed: all five agent.py hunks were the guard and its wraps).

## Tests
- `python3 -m unittest tests.test_tool_labels` — 65 OK (incl. the new pane_send and sweep tests)
- `python3 -m unittest tests.test_panes` — 17 OK
- `python3 -m unittest tests.test_agent` — 46 OK (incl. `test_a_label_bug_cannot_end_the_turn`)
- Probe at the interpreter: `started_label('pane_send', …)` and all three outcome notes verified by hand; before the fix the first raised `NameError` at HEAD.

Verify block met: primary `script` (the three unittest modules above), all green on the landed tree.
