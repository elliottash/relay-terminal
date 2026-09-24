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
- Toggle in Options › Models ("Fall over to a working provider", on by default); side calls do not fail over.
- Owner, 2026-09-19 (follow-on decisions):
  - **Relay Free is opt-in as a target.** A pane running on the user's own key must not be handed to
    Relay's hosted service unless the user allowed it: a second option, "Allow Relay Free as a
    fallback when my own provider keeps failing", off by default, next to the failover toggle. Keyed
    presets of the same tier are tried whatever it says; a pane already on Relay Free is unaffected.
  - **Subagents fail over too**, following the parent's chain (they had no resolver at all).
  - **A routed step whose provider is down drops back to the pane's own model** for the rest of the
    turn, with a note, before any ordinary failover.
  - **The gateway owns the upstream retries**: the hosted client does not retry a refusal the gateway
    has already retried across its own upstreams.

## Tasks
- [x] `RoleResolver.failover_candidates`: same-tier model on every other keyed preset, then Relay Free; never unkeyed, local, or already tried. <!-- t:hd -->
- [x] `Agent._model_call` wrapper: after transport + stall retries, `_begin_failover` swaps provider/config/preset for the rest of the turn; `_end_failover` restores before the terminal event; truncation never moves. <!-- t:pz -->
- [x] `failover` agent option (default on) + Options › Models toggle, applied to a running pane at once. <!-- t:cw -->
- [x] Events/log: `provider_retry {reason: "failover", from_model, to_model}` + `status` + `provider_failover` log line. <!-- t:5m -->
- [x] Tests (11 new), protocol §12.1/§15.2.2, architecture doc. <!-- t:6t -->
- [x] `failover_hosted` agent option (default off) + the Options row; `failover_candidates(allow_hosted=)`; the note names Relay's hosted service. <!-- t:qa -->
- [x] Subagents get the parent's resolver, preset and both switches (`SubagentFactory.failover_options`); an injected provider is still never replaced. <!-- t:rk -->
- [x] `Agent._drop_routing`: a failing plan or vision step ends its routing and finishes on the pane's own model (`provider_retry {reason: "route_dropped"}`, §15.2.3), then the ordinary chain. <!-- t:m4 -->
- [x] Gateway marks a refusal it already failed over with `error.retried`; `HostedChatProvider` treats it as final; `proxy.RETRYABLE_STATUSES` == `ChatProvider.HTTP_RETRY_STATUSES`. <!-- t:w8 -->
- [x] Tests (30 more) across test_failover, test_plan_turns, test_images, test_hosted, test_gateway; protocol §12.1/§15.2/§15.2.2/§15.2.3, docs/RELAY-FREE.md. <!-- t:z2 -->

## QA checklist
Evidence: `docs/qa_evidence/2026-09-19-provider-failover/` (implementer notes, backend test log, build log).

- [ ] A pane whose provider answers 429/5xx past its retries continues the turn on the next keyed preset; the transcript shows “… keeps failing; continuing this turn on …” and the pane's own model is back for the next turn.
- [ ] A Flash pane fails over within Flash models.
- [ ] With no other keys stored and "Allow Relay Free as a fallback" **off**, the turn fails with the original error and never touches Relay Free; with nothing at all, the original error is reported as before.
- [ ] Options › Models › “Fall over to a working provider” turns it off, for new turns and a running pane at once.
- [ ] A truncated step (output limit) ends as before, without a provider switch.

Added 2026-09-19 with the follow-on decisions:

- [ ] Options › Models › "Allow Relay Free as a fallback when my own provider keeps failing" is off on a fresh profile. Turned on, a pane with no other key continues on Relay Free and the note reads "… keeps failing; continuing this turn on Relay's hosted service (Relay Free)."
- [ ] A pane already on Relay Free still fails over to a keyed preset with the new option off.
- [ ] A **subagent** whose provider keeps failing continues on the next keyed preset, and a Flash subagent within Flash. This is the line the 2026-09-19 review could not drive at all: `subagents.py` built its Agent with no resolver, so no subagent ever failed over.
- [ ] A subagent follows the pane's switches: with failover off in Options, a failing subagent ends as before.
- [ ] A **plan turn** whose pinned planning model's provider is down continues on the pane's own model: the transcript shows "Planning model … is not answering; continuing on …", the model chip goes back, and the turn finishes. Same for an **image turn** whose vision model is down *when the pane's own model reads images*; when it does not, the vision failure is reported as before.
- [ ] After such a drop-back, a pane model that also fails still moves to a keyed preset, and the dropped provider is not asked again.
- [ ] Relay Free under load: a hosted 429/503 is retried by the gateway across its upstreams and **not** a second time by the client. **Needs the gateway redeployed** (`docs/RELAY-FREE.md`, "Operating it"); until then the client retries as before, which is the old behaviour and not a regression.
