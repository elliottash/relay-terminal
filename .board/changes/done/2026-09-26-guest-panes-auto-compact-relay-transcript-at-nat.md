---
id: ZGS5
type: work
status: done
labels: [bug, context, guests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
verified_by: openai/gpt-6-sol via codex:ashe-ethz-ch
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane 2026-09-26
links: {plans: [], commits: [40fd7966d12e], evidence: [], related: [CP3M], github: null}
---
# Guest panes auto compact Relay transcript at native 71k limit

## Issue
A Claude Code guest session with a 1M token context repeatedly triggered Relay auto compaction when its saved transcript crossed Relay's 71,232-token native limit, producing small misleading reductions such as 71.8k → 68.8k.

> review compaction, i just saw this compact note which doesnt make sense, as such a small compact would be superfluous
>
> Conversation compacted (auto) ·
> 71.8k → 68.8k tokens
>
> this session: 97d268b4846648f49e6aba30a5ebe433
> — elliott · [session:8d9a841055734e508849fda4e0138f20](relay://session/8d9a841055734e508849fda4e0138f20) · 2026-09-26

## Done means
A guest pane does not automatically compact Relay's saved transcript merely because that transcript exceeds Relay's native context threshold. The guest's own context and manual/model-switch compaction behavior remain available. A focused regression test reproduces the threshold crossing without a compaction event.

## Plan
**Goal:** prevent the guest pane's saved transcript from triggering native automatic compaction.

**Findings:** `backend/relay_core/agent.py::_maybe_compact` still compacts guest transcripts when a summaries role is configured. Session `97d268b4846648f49e6aba30a5ebe433` has a Claude context of about 447k/1M, while Relay's native limit is 71,232; its pane logged repeated instant auto-compactions near 71.8k.

**Steps:** 1. Return early for every guest harness in `_maybe_compact`. 2. Add a focused threshold regression in `tests/test_compact_over_tokens.py`. 3. Update the guest compaction description in `docs/AGENT-SESSIONS-PROTOCOL.md`.

**Risks:** Relay's saved guest transcript can grow until manual compaction or model switching; stale tool outputs still have their separate clearing path.

**Verify:** Run the focused Python tests and inspect the event list and transcript for the guest threshold case.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_compact_over_tokens -v` — 12 tests passed on 2026-09-26.
`PYTHONPATH=backend python3 -m unittest tests.test_agent.GuestAggregateUsageTests -v` — 3 tests passed on 2026-09-26.
Publication queue job `29e53a9dd80c2978` — accepted gate verified and published `8a425ea53ade`.

### Check
Pass: a guest transcript above Relay's 71,232-token limit with a summaries role emits no `compaction_started`; native compaction tests still pass.

## Execution Summary
Session evidence: pane `21c53ef4` for Relay session `97d268b4846648f49e6aba30a5ebe433` logged instant auto-compactions at 15:07:53 and 15:08:26 UTC, including 71.8k → 68.8k. Claude's own context was about 446,618/1,000,000 tokens; Relay's separate native limit was 71,232. The small reduction came from trimming saved tool output at that limit.

Commit `40fd7966d12e8e8618045ea6501275d16541807f` makes `_maybe_compact` return before checking the native limit for guest panes, documents the behavior, and adds a regression. Publication queue job `29e53a9dd80c2978` passed its accepted gate and published `8a425ea53adebbc8ef49ff8a677e9a8acb7e11f2` to `main` at 2026-09-26T15:30:46Z.
