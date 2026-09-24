# Compaction % progress — evidence (card #31BM)

Commit: `231b0918` (feature), plus the evidence commit that added this directory.
Driver: `driver.sh` — Relay under Xvfb, isolated `$HOME`/XDG/TMPDIR, no key, no credits,
against `fake-provider.py` as `local:big` (131,072) and `local:small` (12,000) endpoints.
Run: `RELAY_QA_PORT=<free> RELAY_QA_DISPLAY=<free> driver.sh <repo root> <build dir>`
(any other fake provider on the port makes it exit "port busy"; three earlier runs died to
port collisions and a grep that counted nothing — see git history of this file).

## What the run does

1. `/model local:big`; two 30,000-character "essay" turns (the pane fills; `02-long-on-big.png`
   shows `57% left | big`).
2. `/model local:small` — the switch has to compact first, and the summaries side call streams
   slowly (60 SSE deltas, 0.4 s apart), so the chip is on screen mid-compaction.
3. Shots 03 and 04 during the stream, then a final turn; `05-landed.png` after.

## What the chip shows (tesseract over a 420x60 crop at +880+750, upscaled 5x)

| shot | OCR of the context chip |
|---|---|
| `02-long-on-big.png` | `57% left \| big` |
| `03-compacting-percent.png` | `compacting… 20% \| small` |
| `04-compacting-later.png` | `compacting… 38% \| small` |
| `05-landed.png` | `92% left \| big` |

The percent climbs between the two mid-compaction shots and clears when `compacted` lands.
(The `| small` beside it is the pending model switch, pre-existing UI; after the compaction the
pane reports `small cannot take over: even compacted …` — the refusal is expected here and
unrelated: the fixture's essays plus Relay's tool definitions never fit a 12,000-token window.)

An earlier run with a 6,000-character fixture summary pinned the chip at `compacting… 95%`
immediately — the estimate floor (2,000 chars) was below the summary length, so the display
sat on the 95% clamp for the whole stream. That is the clamp doing its job, not a defect.

## Protocol evidence

`requests.jsonl` (timestamped by the fake provider): the two essay turns, then the no-tools
request carrying "Transcript of the earlier conversation" (the compaction summary, `summary:
true`) spanning ~24 s of stream, then the following turn. `log-lines.txt` records the worker's
`model_selection … applies=after_compaction` line; the GUI's relay.log does not persist
compaction events, which is why the driver waits on fixed timing rather than log greps.

## Tests

`tests/test_compaction_progress.py` (4 tests): sidecall forwards deltas (`on_delta`), the
summary counts content only and clamps the estimate, `expected_chars` is the denominator, and a
full `Agent.compact()` emits `compaction_progress` between `compaction_started` and `compacted`
with growing `chars`, positive `estimate` and `phase: summary`, then learns `summary_chars`.
Neighbours re-run green: `tests.test_compact_over_tokens`, `tests.test_agent_context`,
`tests.test_provider`, `tests.test_tool_output_bounds`, `tests.test_sessions`,
`tests.test_requests`, `tests.test_loopdetect` (the one error was an import-path artifact of
invoking `test_requests` without `tests/` on `PYTHONPATH`; re-run green with it).
