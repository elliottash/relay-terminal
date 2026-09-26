---
id: EBGX
type: work
status: planned
labels: [bug, board]
rank: zzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: a test writes an apostrophe into every string field of a card and the file round-trips with all keys intact, sign_off: none, effort: low, blast: capability}
links: {related: [SZHQ]}
---
# board_update_card: an apostrophe in verify.criteria swallows the rest of the card's front matter (found while writing #SZHQ)

## Issue
can you look at the time spent and how many tokens were spent and on what tasks, to see if there are any lessons for the app. because what we do here is going to be happening on all the users' machines by default.

## ## Issue
While landing #SZHQ, `board_update_card` wrote this into the card's front matter:

```
verify: {artifact: code, primary: script, also: [probe], human: none, criteria: '... no live session''s tree removed, sign_off: none, effort: low, blast: capability}
source: ...
links: {...}
```

The apostrophe in the criteria string broke the flow-mapping writer: everything after `session''`s` — the rest of the `verify` mapping and the whole `source:` and `links:` lines — was absorbed into the `criteria` value. #SZHQ lost its provenance and links until the front matter was repaired by hand, and the card's own YAML stopped parsing as intended.

**How to address.** (1) The board writer should emit front matter as block-style YAML (`yaml.safe_dump(..., default_flow_style=False)`, or dump the whole header with the existing library) instead of hand-building flow mappings with single quotes. (2) After every write, re-parse the file and assert the keys round-trip (a one-line `safe_load` check in the write path catches this class for every card, apostrophe or not). (3) Add the test named in Done means, with an apostrophe, a colon and a brace in each string field.

## ## Done means
- A card write whose string fields contain `'`, `:`, `,`, `{` and `}` produces a card file that re-parses, with the board's own parser, to exactly the keys and values that were sent — sibling keys (`sign_off`, `source`, `links`) survive the next read *and* the next write.
- The save path re-parses what it is about to write and refuses to save a card whose front matter does not round-trip (writes nothing on refusal).
- Failure would look like: criteria holding an apostrophe still swallows `sign_off`/`source`/`links` on the following write (the #SZHQ symptom) — or the new guard rejects a value a standard YAML reader accepts.

## Plan
**Goal.** An apostrophe in any front-matter string field (as in #SZHQ's `verify.criteria`) must no longer swallow the rest of the card's front matter, and a card write must never save front matter that does not re-parse to what was sent.

**Findings.**
- `backend/relay_core/board.py` carries a deliberate hand-rolled YAML subset; there is **no PyYAML dependency anywhere in the backend** (`import yaml`/`safe_load`: zero matches; the #switchboard-phase0 QA treated PyYAML as optional). `scripts/relay-board.py` imports `board.py`, so there is exactly one writer/parser pair — the fix is Python-only. The lone `bad_front_matter` reference in `tests/boardmodel_test.cpp:3746` is a fake expectation; C++ never parses card YAML itself.
- The writer is YAML-correct: `_quote` (board.py:577) doubles `'` to `''`; `yaml_value` (626) and `dump_front_matter` (634) hand-assemble flow mappings.
- The parser is not: `_split_flow` (655–679), `_split_key` (~682), `_strip_comment` (~721) and the balanced scanner (~740) toggle quote state on a bare `if ch == quote:`. They never treat `''` as an escape, so in `criteria: '...session''s …'` the first `'` closes the span and the second reopens one that swallows everything after it. `_parse_scalar` (640) already undoes `''` → `'`; the asymmetry is purely in the scanners. #MSJ0 (board.py:617) is the prior incident in this family.
- The corruption becomes permanent on the next write: a dirty card re-dumps the *parsed* dict (`Card.to_text`, ~992–995), which is how #SZHQ lost `sign_off`, `source` and `links`.
- Existing coverage to extend: `tests/test_board.py` — `test_round_trip_keeps_unusual_front_matter_bytes` (:183), the `dump_front_matter`/`parse_yaml` round-trip (:209), `verify_block` round-trip (:536).

**Steps.**
1. Fix the quote scanners — `_split_flow`, `_split_key`, `_strip_comment` and the balanced scanner: while inside a `'`-quoted span, a `'` followed by another `'` consumes both and stays in the span; a lone `'` closes. Mirror of what `_parse_scalar` already does on the way out.
2. Add the write-path guard at the save (`board_tools.py:893`, `B.atomic_write(path, self.markdown())`): after dumping the new front matter, `parse_yaml` it back and compare keys/values with the front dict about to be saved; on any mismatch, refuse with an error naming the key and the code `bad_front_matter` (shape already expected in `tests/boardmodel_test.cpp:3746`) and write nothing.
3. Tests in `tests/test_board.py`: (a) dump/parse round-trip with `'`, `:`, `,`, `{`, `}` in `verify.criteria`, `source` and a label; (b) the #SZHQ reproduction — a card whose criteria holds `session's`: write, re-read (all sibling keys intact), write again (idempotent bytes); (c) the guard: front matter that would not round-trip is refused and the file is left byte-identical.
4. Optional hardening, only if trivial: emit multi-key mappings like `verify` in block style from `dump_front_matter`, so flow-scalar quoting is used less. Skip if it rewrites unrelated cards' front-matter bytes.

**Risks.**
- The card's Issue suggests `yaml.safe_dump` (PyYAML). The backend has no PyYAML dependency and must parse boards with only the bundled Python, so adding it is an owner decision; this plan fixes the existing subset instead and needs no new dependency. If you would rather vendor PyYAML, say so before Run.
- The scanners are shared: `board.yaml`'s `filter: status:done,dropped` relies on comma splitting inside flow collections (board.py:3303). Run the parser's other consumers, not just the board tests.

**Verify.**
- `python3 -m pytest tests/test_board.py` (new tests + existing round-trip tests), plus `tests/test_agents_defs.py` and `tests/test_signal_threads.py`, which call `parse_yaml` directly.
- By hand on a scratch card: set `verify.criteria` containing `session's` via the board tools, re-read, confirm `sign_off`/`source`/`links` survive two writes — matching the card's own `verify.criteria` (apostrophe in every string field, all keys intact).
