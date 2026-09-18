---
id: G5MK
type: work
status: needs-qa-llm
component: [worker]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Opus 5 (1M context), Claude Code session, 2026-09-18
rank: zzz
created: '2026-09-18'
labels: [bug]
acceptance: 'a model call that reaches the output limit having produced nothing is taken again once with a note instead of failing the turn; a cut-off answer that did reach the user is kept in the conversation; the tokens a cut-off call spent are counted; the failure message names the real budget and does not tell the user to raise a limit that is already at its maximum'
source: 'owner, 2026-09-18: session 270a38a3c36d4201b452f2206d5861b6, "Response was truncated or filtered; partial tools were not executed. Increase output limit or narrow the task."'
links: {plans: [], commits: [], evidence: [], related: [27YQ, SQAM], github: null}
---
# A step cut off at the output limit threw the whole turn away

## What happened (owner's session 270a38a3c36d4201b452f2206d5861b6, 2026-09-18 22:25–22:29 UTC)

A GLM-5.3 pane (Coding Plan, mode `build`, effort `high`) was working on "in the light theme, the
terminal and agent colors (cyan and violet) are too dark and desaturated". It ran for four minutes
and nine tool calls — reading the theme test, computing contrast ratios, searching for candidate
colours. On the tenth step it thought for 108 seconds, produced no answer text and no tool call, and
the stream ended with `finish_reason: "length"`.

```
turn_end … outcome=error ms=239658 thinking_ms=224291 tools=9 retries=0
  error="Response was truncated or filtered; partial tools were not executed.
         Increase output limit or narrow the task."
```

Three things were wrong with that, none of them the model's fault:

1. **The turn was lost.** Nine tool results, four minutes, and the request stayed open with nothing
   to show. A step that produced nothing is exactly the case that can be taken again safely — the
   same argument as the stall retry (SQAM): a model call only happens at a step boundary, where
   every earlier tool call already has its result, so nothing is in flight.
2. **The advice was impossible to follow.** "Increase output limit" — the limit was 32768, which is
   both the default (27YQ) and the top of the allowed range, so there was nothing to increase. The
   sentence also said nothing about the actual cause: `max_tokens` is the budget for one call and
   reasoning is spent from it, so at effort `high` a model can use all of it thinking.
3. **The tokens vanished.** `emit({"event": "usage"})` sat *after* the `raise`, so a cut-off
   response — the single largest request of the turn — was counted neither in the session's usage
   total nor by the context tracker that decides when to compact.

## Change

**`backend/relay_core/provider.py`**

- New `ProviderTruncated(ProviderError)` carrying `reason` (`length` / `content_filter`),
  `max_tokens`, `produced` (did any of it reach the user) and `partial` (the answer text worth
  keeping). Its message names the budget, says reasoning is spent from it, and gives the remedy the
  user actually has: lower the effort when the limit is already at Relay's maximum, raise the limit
  when it is not.
- `usage` is emitted before every refusal at the end of the stream, so a cut-off call's tokens are
  counted.
- `MIN_OUTPUT_TOKENS` / `MAX_OUTPUT_TOKENS` name the 256–32768 range that was written as literals.
- `_partial()` keeps a cut-off response's answer text only when it carries no tool calls: truncated
  arguments are half-written JSON, and an assistant message with tool calls and no results is not a
  conversation a provider accepts.

**`backend/relay_core/agent.py`**

- `_model_call` takes a cut-off step again **once per turn** (`MAX_TRUNCATION_RETRIES`), and only
  for `length`, only when nothing was produced, and only when the turn was not cancelled. A
  `content_filter` refusal is never retried: it would repeat.
- Before the retry a `relay_kind: "note"` message joins the conversation naming the budget, saying
  that reasoning is spent from it, and asking for one small step. Without it the retry is the same
  request and truncates the same way.
- When there is no retry, a partial answer is added to the conversation before the error, so the
  next turn can carry on from the text the user watched appear.
- `provider_retry {reason: "truncated"}` and a `provider_truncated` log line; the pane already
  prints `provider_retry.text` as a note, so no GUI change.

**`backend/relay_core/keytest.py`** matched the old message *text* to decide that a truncated key
test is a pass. Rewording the sentence would have turned every reasoning model's key test into a
failure. It now matches the exception type, and a `ProviderError` subclass's message (a stall's, for
instance) is shown instead of "Test failed (ProviderStalled)."

**`docs/AGENT-SESSIONS-PROTOCOL.md`** section 15.2.1.

Effort is deliberately **not** lowered for the retry: what to do about a model that thinks for two
minutes a step is the owner's call, and the note asks for the same thing without changing a setting
behind their back.

Files: `backend/relay_core/provider.py`, `backend/relay_core/agent.py`,
`backend/relay_core/keytest.py`, `tests/test_provider.py`, `tests/test_agent.py`,
`tests/test_keystore.py`, `docs/AGENT-SESSIONS-PROTOCOL.md`.

## QA checklist

1. Point a pane at a mock endpoint that streams reasoning and then `finish_reason: "length"` on the
   first call and a normal answer on the second: the transcript shows one `⚠` note line and the turn
   finishes. The retried request carries one extra note message.
2. Same mock, truncating twice: the turn fails, the message names the 32768-token budget and says
   the limit is already at Relay's maximum, and the request stays open in the ledger.
3. Mock that streams `"Half an ans"` and then truncates: the turn fails, and the partial text is in
   the saved session's messages.
4. Keys modal › Test on a reasoning provider: still passes with `truncated`.
5. `./scripts/test.sh` and `ctest` pass.
