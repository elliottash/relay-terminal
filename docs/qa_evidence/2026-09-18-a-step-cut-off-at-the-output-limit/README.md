# Card #G5MK — a step cut off at the output limit

Implementer evidence, 2026-09-18. `drive.py` runs a real `Agent` and a real `ChatProvider` against a
mock SSE endpoint on loopback that streams reasoning and then `finish_reason: "length"`. No provider
key and no network. `mock-output-limit.log` is its output for the three cases.

```
XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=backend \
  python3 docs/qa_evidence/2026-09-18-a-step-cut-off-at-the-output-limit/drive.py retried
```

| Case | What the mock does | What Relay does |
|---|---|---|
| `retried` | reasoning only, then `length`; the second call answers | one `provider_retry` note, a `relay_kind: "note"` message added, the turn finishes and the request closes |
| `twice` | `length` on both calls | the turn fails after one retry, the request stays open, and **both** calls' tokens are counted (65536 completion tokens) |
| `partial` | answer text, then `length` | no retry — the text was on the user's screen — and `"The cyan reads dark because "` stays in the conversation, before the "not finished" note |

Two things to read in the log:

- **The message names the real remedy.** With the limit at 32768 it says the limit is already at
  Relay's maximum and to lower the effort. The old sentence said "Increase output limit", which at
  the default was impossible. With a lower limit configured it says to raise it
  (`tests/test_provider.py::test_a_lower_output_limit_is_told_to_raise_it`).
- **`usage counted:`** is non-zero after a cut-off call. It used to be dropped: the `usage` event was
  emitted after the `raise`, so the session total and the context tracker missed the largest request
  of the turn.

The owner's original failure is `~/.local/share/relay/sessions/83e6ce670835f99d/270a38a3c36d4201b452f2206d5861b6.json`
and `worker.log` at 2026-09-18T22:29:58.403Z (`turn_end … ms=239658 thinking_ms=224291 tools=9`).
