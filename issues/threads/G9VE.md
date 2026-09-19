<!-- relay:entry 20260919T050222Z-8q author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent created this card in Ready · issues/features/2026-09-19-fail-over-to-another-provider-when-one-keeps-fai.md

<!-- relay:entry 20260919T050233Z-cc author=agent kind=question model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
Which providers may a failing turn fall over to? My recommendation: only ones already usable without any setup — other presets with a stored key (order: the pane's own provider first, as today; then other keyed presets; then Relay Free as the last resort), never a provider that would silently spend a key the user did not choose. The switch would happen after the transport's retries are exhausted (six today, on 429/5xx only), announce itself in the transcript the way provider_retry does, and last for the rest of the turn only — the pane's own provider is not reconfigured. Answer here or in the terminal and I'll build it.

<!-- relay:entry 20260919T050242Z-2q author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260919T054939Z-er author=agent kind=decision model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
"yes exactly: other keyed presets from the same tier (main / flash), then Relay Free. this can be disabled in the models options page" — pane 1, 2026-09-19.

<!-- relay:entry 20260919T054948Z-dd author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · waiting_on: owner → (unset)

<!-- relay:entry 20260919T054956Z-e0 author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260919T054959Z-rt author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent moved this card · Ready → In progress · Owner answered the candidate question; building it now.

<!-- relay:entry 20260919T055415Z-a8 author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · appended to `## Tasks`

<!-- relay:entry 20260919T055420Z-bx author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T055426Z-bv author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented on the exact tree (build + backend tests green); checklist and evidence on the card. · evidence docs/qa_evidence/2026-09-19-provider-failover/

<!-- relay:entry 20260919T183000Z-rv author=agent kind=note model=claude-fable-5.1 -->
Review findings fixed on main, 2026-09-19. A provider that had already streamed part of an answer no
longer hands the turn on (a per-call `produced` flag, the rule 15.2 uses for the stall retry); the
failed provider's open HTTP response is closed before the swap; the swap now goes through the same
code as `set_model` (history adapted to the new provider's dialect, context window, `max_tokens`,
effort) and the restore undoes all of it; `_end_failover` runs in `_end_turn` beside
`_end_vision_turn`, so the restore and its "Back to <model> (<preset>)." note
(`provider_retry {reason: "failover_ended"}`) come before the turn's terminal event; a `set_model`
arriving mid-failover waits for the turn's end instead of being overwritten; the Flash/Main decision
reads the preset's Flash row directly, so an OpenRouter or local pane is no longer mistaken for a
Flash one; two keys on one host (glm + glm-coding) count as one provider; a chain that ends in
failure reports the *first* provider's error, prefixed with what else was tried, and carries only
that error's `code`/`resets_at`; a resolver that raises is logged (`provider_failover_unavailable`)
instead of swallowed; the note is emitted after the thinking block is closed. Protocol 15.2.1/15.2.2
are in order and at the same depth, the `provider_retry` schema lists every reason it has, and
`failover_candidates`' docstring no longer claims an order it does not have.

One QA checklist item cannot be driven as written: **"A Flash pane/subagent fails over within
Flash"** — the subagent half is untestable, because `subagents.py` builds its `Agent` without a
roles resolver, so a subagent never fails over at all (by design, per the card). The pane half is
covered by `tests/test_failover.py`. Whether subagents should fail over is the owner's call; giving
them a resolver is a change to `subagents.py`, outside this review's scope.

<!-- relay:entry 20260919T210000Z-fo author=agent kind=note model=claude-fable-5.1 -->
Four follow-on owner decisions of 2026-09-19 built on main (`903f69f`, plus the GUI row and the
protocol section in a second commit).

**Relay Free is opt-in as a failover target.** The one candidate that is not already the user's own
provider: another company's terms, a shared allowance. `failover_hosted` (agent option, default
false) gates it, `failover_candidates` takes `allow_hosted=`, keyed presets of the same tier are
tried whatever it says, and the value is decided once per turn beside the tier so a hosted spare
cannot widen the chain on the second move. A pane already on Relay Free passes True. The note names
it for what it is: "… keeps failing; continuing this turn on Relay's hosted service (Relay Free)."

**Subagents fail over, on the parent's chain.** This is the checklist line the earlier review said
could not be driven. `subagents.py` built each subagent's `Agent` with no roles resolver, and
`_begin_failover` refuses every move without one, so no subagent had ever failed over. The factory
now hands on the pane's resolver, its preset and both switches, read from the pane at spawn so a
`set_agent_options` reaches a queued subagent. An injected provider is still never replaced. Giving
the subagent the resolver also lets the compaction summary it makes use the Summaries role, as the
pane's does — consistent, and worth noting because it is a second effect of the same line.

**A routed step whose provider is down drops back to the pane's own model.** A failover was refused
while a plan or vision swap was up, so a plan turn on a dead pinned planning model failed outright.
`_drop_routing` ends the routing, emits `provider_retry {reason: "route_dropped"}` plus a `status`
(logged `provider_route_dropped`) and finishes the turn on the pane's own model; the ordinary chain
follows only if that fails too, with the dropped provider already marked as tried. Not gated on the
`failover` option — a return to the model the pane already has is not a move to someone else's. One
exception: an image turn is not dropped back when the pane's own model cannot read images, since that
is why it was routed; the vision failure is reported as before.

**The gateway owns the upstream retries.** A refusal returned after more than one upstream carries
`retried` in the gateway's error body; `HostedChatProvider` treats it as final. A `rate_limited`
window the gateway reports is still waited out — that is the gateway's own door, not an upstream's.
The gateway's retryable set is now `proxy.RETRYABLE_STATUSES`, equal to the client's
`HTTP_RETRY_STATUSES` (501 and 505 are as final as a 404), with a test asserting the equality since
the gateway box cannot import `relay_core`. **The gateway must be redeployed for this half to take
effect** (docs/RELAY-FREE.md, "Operating it"); nothing was deployed from here.

