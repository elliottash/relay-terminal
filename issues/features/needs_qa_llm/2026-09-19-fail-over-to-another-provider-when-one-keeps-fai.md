---
id: G9VE
type: work
status: needs-qa-llm
labels: [feature, providers, routing]
implemented_by: claude-opus-4-5
rank: zzzzs
created: '2026-09-19'
source: 'comment on #VMZP, 2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-provider-failover/], related: [], github: null}
---
# Fail over to another provider when one keeps failing a turn

## Issue
for model retries, if a model doesnt work, try another provider after 3 tries or something

## Decisions
- Order: other keyed presets of the same tier (Main or Flash), then Relay Free; never a provider without a stored key, never a local endpoint, never one already tried this turn (owner, pane 1).
- Each candidate is asked once, after its own transport retries (#VMZP); at most two besides the pane's own; the swap lasts for the rest of the turn only.
- Toggle in Options › Models ("Fall over to a working provider", on by default); subagents and side calls do not fail over.

## Tasks
- [x] `RoleResolver.failover_candidates`: same-tier model on every other keyed preset, then Relay Free; never unkeyed, local, or already tried. <!-- t:hd -->
- [x] `Agent._model_call` wrapper: after transport + stall retries, `_begin_failover` swaps provider/config/preset for the rest of the turn; `_end_failover` restores before the terminal event; truncation never moves. <!-- t:pz -->
- [x] `failover` agent option (default on) + Options › Models toggle, applied to a running pane at once. <!-- t:cw -->
- [x] Events/log: `provider_retry {reason: "failover", from_model, to_model}` + `status` + `provider_failover` log line. <!-- t:5m -->
- [x] Tests (11 new), protocol §12.1/§15.2.2, architecture doc. <!-- t:6t -->

## QA checklist
Evidence: `docs/qa_evidence/2026-09-19-provider-failover/` (implementer notes, backend test log, build log).

- [ ] A pane whose provider answers 429/5xx past its retries continues the turn on the next keyed preset; the transcript shows “… keeps failing; continuing this turn on …” and the pane's own model is back for the next turn.
- [ ] A Flash pane fails over within Flash models.
- [ ] With no other keys stored the turn lands on Relay Free; with nothing at all, the original error is reported as before.
- [ ] Options › Models › “Fall over to a working provider” turns it off, for new turns and a running pane at once.
- [ ] A truncated step (output limit) ends as before, without a provider switch.
