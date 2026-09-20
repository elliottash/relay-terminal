# #VN69 — leaked tool-call JSON stripped from card answers

Card: #VN69 (Malformed tool-call JSON leaks into a card thread's agent reply)
Date: 2026-09-20 · Implemented on pane 3 (Switchboard session)

## What the fault was

The glm-5.3 gateway broke a `board_update_card` call mid-argument and streamed the rest of
the tool call (an Anthropic-style `{"type": "tool_use", "id": "toolu_bdrk_…"}` block) as
**content**. `provider._stream` concatenates content verbatim, hosted endpoints get no tidy
pass, and `board_turns._observe` joins every delta into the answer `_card_answer` writes to
the card's thread — so the fragment reached thread entry `20260920T040604Z-st` on #Y2JW.

## Before (reproduction of the measured fragment through the old join)

Input fed as content deltas:

```
Recording the decisions on the card:

The Decisions text got truncated by a stray quote — fixing the section:_resolver: fe"tool_use_id": "toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB"
{
  "type": "tool_use",
  "id": "toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB",
  "name": "board_update_card",
  "input": {"id": "Y2JW", "section": "Decisions"}
}

Redoing the section fix properly. Done.
```

Old behaviour: the whole text, JSON included, landed on the thread.

## After (`strip_tool_fragments`, new in localtext.py, applied at three layers)

Thread-bound answer after `board_turns._observe` strips the join:

```
Recording the decisions on the card:

The Decisions text got truncated by a stray quote — fixing the section:_resolver: fe
Redoing the section fix properly. Done.
```

The JSON is gone; the prose on both sides stays; the half-word `_resolver: fe` (the tail of
the string the model broke mid-word) survives by design — see the Plan on the card.

## Tests

- `tests/test_localtext.py` · `tests/test_provider.py` · `tests/test_board_turns.py`:
  **87 tests, OK** (includes the new StripTests, the stream and partial cases, and
  "a leaked tool call never reaches the card thread").
- Regression neighbours: `test_plan_turns` 13 OK, `test_provider_local` 22 OK.
- `test_board_protocol`: 2 failures (card stage moves) that also fail on the pre-change
  snapshot restored from land's snap — pre-existing from another session's work, unrelated.

Live pane display still shows what the provider streamed (deltas are already on the wire);
the fix cleans what is stored, replayed and written to threads — recorded as accepted scope
in the card's QA checklist.
