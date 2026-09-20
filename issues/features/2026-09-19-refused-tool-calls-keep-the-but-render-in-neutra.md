---
id: 25XG
type: work
status: planned
labels: [feature, ux]
rank: zzzzzzzw
created: '2026-09-19'
source: 'pane 1, 2026-09-19 (Discuss on #SFP6)'
links: {plans: [], commits: [], evidence: [], related: [SFP6], github: null}
---
# Refused tool calls keep the ✗ but render in neutral ink, not red

## Issue
i agree that non-actionable "errors" like this one should have the x but not in red. plan that

## Plan
## Goal

A tool call a guard deliberately refused (edit_file's 128 KiB cap, path guards, secret guards, budget) keeps its ✗ and its message but wears the neutral tool ink instead of red. Red stays strictly for *failed*; amber keeps its one job (waiting on a person). Decision recorded on #SFP6, 2026-09-19.

## Findings

- Every guard refusal in `backend/relay_core/tools.py` is a `raise ValueError(…)` (52 sites: size caps at :387/:809/:978, path/secret guards at :344–375, arg validation). Runtime failures raise `OSError` instead; there is no other tool exception type.
- One catch site turns both into results: `backend/relay_core/agent.py:1415` `except (OSError, ValueError, UnicodeError)` → `result = {"error": str(exc)[:2000]}` (:1416), wrapping both `_prepare` (:1406) and `_execute` (:1414) — so the card's screenshot case (prepare-time over-size) flows through here. The budget result at :1384 (`{"error": "Tool budget reached…"}`) is the same grade. `UnicodeError` is a subclass of `ValueError`, so clause order matters.
- The label is computed by `tool_labels.result_label` (`backend/relay_core/tool_labels.py`) at event time (:1399/:1420) and recomputed from the stored result for `turn_summary`/`tool_output_get`, so a flag in the result dict survives replays. `call_ok` (not ok → ✗, no merge, fold on click) must stay as it is. Note `_error_message` already treats a *string* `refused` code (program_input/terminal_handoff results like `{ok: false, refused: "busy"}`) — that is a different, runtime thing and stays red.
- The ink is the surface's, not the backend's (§ 23). Red sites today:
  - terminal pane: `src/Pane.h:4745` (`row.failed ? Ink::Error : Ink::Tool`), `:4935` (replay bytes), `:9262` (the no-fold-layer `tool_result` print), and the cached row at `:4687` (`hidden.failed = row.failed`);
  - `src/SubagentTranscript.cpp:236` (`call.label.failed() ? Ink::Error : Ink::Tool`);
  - `src/TurnTranscript.cpp:76` (`ok ? Text : SyntaxUnknown`) and `:233`;
  - `src/AgentInternalsView.cpp` `drawRow()` (`call.done && call.label.failed() ? Ink::Error : Ink::Tool`).
- `Ink::Tool` already maps to `theme::TextMuted` (`src/Pane.h:11180`) — the neutral ink exists; no new theme token is needed. `calllines::Row` (`src/CallLines.h`) carries `failed` but not the refusal grade; `toollabel::Label` parses `ok` but not `refused`.
- The colour table is `docs/ARCHITECTURE.md:2476` (`error` (red) = "failed, …"), with the "Amber has one job" paragraph at :2481 — where a matching "red is only for failures" sentence belongs. The label contract is the table at `docs/AGENT-SESSIONS-PROTOCOL.md:3278–3284` (§ 23.2).
- BoardPane shows its agent's calls as progress text ("failed: …", `src/BoardPane.cpp:4856`), not ✗ rows — no ink change there.

## Steps

1. **Backend, mark the refusal** — `backend/relay_core/agent.py` ~1415: split the except so a `ValueError` (a guard's deliberate refusal) sets the flag: `except (OSError, UnicodeError) as exc:` → `result = {"error": str(exc)[:2000]}` (UnicodeError listed *first* — it is a ValueError subclass and a decode error is a failure, not a refusal), then `except ValueError as exc:` → `result = {"error": str(exc)[:2000], "refused": True}`. Add `"refused": True` to the budget result at :1384. The flag rides the stored result and therefore the model's tool message too — intended, harmless.
2. **Backend, label** — `backend/relay_core/tool_labels.py` `result_label()`: after `ok = call_ok(result)`, add `if result.get("refused") is True: label["refused"] = True`. Nothing else changes: `call_ok` still False (✗, no merge, fold-on-click), `_error_message` still supplies the text after the ✗, and a string `refused` code (`is True` is strict) does not fire.
3. **Wire contract** — `docs/AGENT-SESSIONS-PROTOCOL.md` § 23.2 table: add `| refused | bool | on tool_result only. A guard's deliberate refusal (a ValueError from the tool's guards): the call did not happen and nothing failed. Surfaces keep the ✗ but draw it in the neutral tool ink, never the error ink |` after the `ok` row.
4. **Parse it** — `src/ToolLabel.h`/`.cpp`: `Label` gains `bool hasRefused = false, refused = false;`, parsed from the label object exactly as `ok` is. `failed()` unchanged. `src/CallLines.h`/`.cpp`: `Row` gains `bool refused = false;` (comment: the ink grade — error when merely failed, tool ink when refused); `finishedRow()` sets it from the label and keeps appending `" ✗"`.
5. **Terminal pane** — `src/Pane.h`: at :4745, :4935 and :9262 change `failed ? Ink::Error : Ink::Tool` to `failed && !refused ? Ink::Error : Ink::Tool` (row.refused at the first two, label.refused at :9262); carry `refused` beside `failed` in the cached hidden row at :4687. A refused row is then TextMuted like any tool row, with its ✗ and message.
6. **Other surfaces** — same one-condition change: `src/SubagentTranscript.cpp:236`, `src/AgentInternalsView.cpp` `drawRow()`, and `src/TurnTranscript.cpp` (addRow :76 and setToolOutput :233: refused failed rows take `theme::TextMuted` instead of `SyntaxUnknown`; the ✗ marker stays everywhere).
7. **Colour table** — `docs/ARCHITECTURE.md` beside the table (~:2481): one short paragraph "**Red is only for failures.** A tool call a guard refused on purpose keeps its ✗ but wears the neutral tool ink (#25XG); amber still means waiting on a person."

## Risks

- **Which refusals count.** This plan marks guards' `ValueError`s (and the budget message) per the recorded decision. Board tools return coded results instead (`board_refused`, `board_mode_refused`, `board_confused`…) and program-input refusals carry a string code; those stay red here. *Question for the owner:* fold `board_mode_refused` (Plan/Discuss-mode write attempts, common in this repo) in as a one-line addition to `BoardToolError.to_result()`? Recommendation: yes, as a follow-up card once this lands — the hash-conflict codes name an action and should stay red.
- **UnicodeError ordering** is the one real trap: catching `ValueError` first would paint decode failures as refusals. Step 1's clause order is load-bearing; test both.
- A refused row is visually identical to a successful one in the terminal beyond the ✗ (both TextMuted). If that reads too flat, a dim attribute on the span is the escape hatch — try the plain neutral first.
- Line numbers above are from 2026-09-20; grep before editing, and build/test through `scripts/relay-build` and `python3 scripts/land.py begin|commit` per repo rules.

## Verify

- `python3 -m pytest tests/test_tool_labels.py -k "refus or error" -q` — new cases: a result with `refused: True` yields a label with `refused` and `ok: False`, no `merge`; a string `refused` code does not; an `OSError` result does not.
- Extend `tests/test_agent.py`: a fake tool raising `ValueError` → `tool_result` event's label carries `refused: true` and the model-facing result too; one raising `OSError` (and a `UnicodeDecodeError`) → no flag.
- `ctest --test-dir build -R "toollabel|calllines"` — `toollabel_test.cpp`: `refused` parses, `failed()` still true; `calllines_test.cpp`: `finishedRow` on a refused label sets `failed` and `refused` and keeps the `" ✗"` suffix.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: have the agent `edit_file` on a >128 KiB file (`src/BoardPane.cpp`) — expect `▸ edit BoardPane.cpp ✗ · File exceeds the 128 KiB preview/read limit.` in the muted tool ink; then `run_command ls /nonexistent` — still red. Screenshot both into `docs/qa_evidence/2026-MM-DD-refused-neutral-ink/` and move the card to `needs_qa_llm` with the checklist.
