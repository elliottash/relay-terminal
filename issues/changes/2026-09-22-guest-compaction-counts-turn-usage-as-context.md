---
id: CP3M
type: work
status: inbox
labels: [bug, context, guests]
assignee: null
rank: m
created: '2026-09-22'
source: 'Codex in a Relay pane, 2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [C8WX], github: null}
---
# Guest aggregate usage triggers spurious compaction and inflated token savings

## Issue
can you check how compaction works. 

i just saw (in this  convo e0711a2a) 

Conversation compacted (auto) · 3.1M → 54.3k tokens

could that be right? that seems like a bug in the numbers

## Execution Summary
Investigation confirmed the accounting bug; implementation remains open.

Pane e0711a2a maps to Relay session 678e5e19bfda407591a623f02e9421f6 and Codex thread 01a0ca52-9e46-74f1-beb5-95c16515d325. The GUI log records compaction_started and compacted at the same millisecond, 2026-09-23T00:40:13.679Z. The saved Relay session has epoch 0 and no summary boundary.

The Codex rollout's last usage before the preceding turn was 9,736,903 cumulative tokens (2026-09-22T19:15:49.130Z). At that turn's end (2026-09-23T00:24:21.647Z), it was 12,842,368: a delta of **3,105,465** tokens. The last individual request used **181,959** tokens in a **258,400** token window. Thus 3.1M is the aggregate across calls, not the context size.

`guest_harness_codex.CodexHarness._usage_for` intentionally emits the turn usage delta for accounting and a separate context_tokens measurement. `guest_harness_provider.relay_usage` maps the aggregate to total_tokens. `Agent.ask` passes it to `ContextTracker.record_usage`, which treats it as one request and also inflates its estimate calibration (up to 4x). `_maybe_compact` uses that inflated count when a separate summaries role is available. `Agent.compact` then invalidates the measurement and reports an estimate as the after count, even if `context.compact` returns unchanged messages. The 54.3k is therefore a Relay transcript estimate, not evidence that Codex's live context shrank to 54.3k. Its exact value was not independently reconstructed.

Local evidence: ~/.local/share/relay/logs/relay.log, ~/.local/share/relay/logs/worker.log, ~/.local/share/relay/sessions/83e6ce670835f99d/678e5e19bfda407591a623f02e9421f6.json, and ~/.codex/sessions/2026/09/22/rollout-2026-09-22T14-13-27-01a0ca52-9e46-74f1-beb5-95c16515d325.jsonl. Only usage metadata was extracted from the private rollout.

## Done means
Guest turn totals continue to count toward usage, but never serve as a single-request context measurement or calibrate Relay's transcript estimate. Compaction decisions and before/after counts use a consistent basis. An unchanged transcript does not claim a reduction caused solely by changing accounting methods. Guest context remains distinct from Relay's retained transcript.

## Tests
Manual: reproduced with ContextTracker.record_usage(total_tokens=3105465, guest_context_tokens=181959), an unchanged four-message transcript through context.compact, and invalidate(): before=3105509, after=172, summary_chars=0, messages_unchanged=True, ratio=4.0. No provider call was made.
