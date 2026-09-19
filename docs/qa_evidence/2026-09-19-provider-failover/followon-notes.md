# Provider failover — the 2026-09-19 follow-on decisions

Card: `issues/features/needs_qa_llm/2026-09-19-fail-over-to-another-provider-when-one-keeps-fai.md`
(#G9VE). Thread: `issues/threads/G9VE.md`. These notes cover the four decisions the owner made after
the first implementation landed; the original notes are in `implementer-notes.md` beside this file.

## C1 — Relay Free is opt-in as a failover target

A pane running on the user's own key must not have its conversation handed to Relay's hosted
service unless the user allowed it: another company's terms, a shared allowance. Every other
candidate is a provider they set up themselves with a key they stored, which is a move inside what
they already chose.

* New agent option `failover_hosted` (bool, default **false**, protocol 12.1), plumbed exactly like
  `failover`: `agent/failover_hosted` in settings → `Pane::requestOptions()` → `configure` /
  `set_agent_options` → `validate_turn_options` → `Agent.failover_hosted` → `options()` →
  `configured` / `agent_options`. It applies to a running pane at once, from the same key list.
* `RoleResolver.failover_candidates(..., allow_hosted=False)` gates Relay Free **and nothing else**:
  keyed presets of the same tier are returned whatever it says.
* `Agent._begin_failover` decides the value **once per turn**, stored on the swap beside the tier:
  `self.failover_hosted or self._on_hosted()`. By the second move `self.config` is already a spare
  provider's, and a spare that happened to be hosted would answer "yes" for a pane that never
  ticked the box.
* A pane already running on Relay Free (`_on_hosted()`: the config or the preset is hosted) passes
  True — its conversation is going through the gateway already, so there is nothing left to opt into.
* Options › Models gains one row under "Fall over to a working provider": **"Allow Relay Free as a
  fallback when my own provider keeps failing"**, off by default.
* When the turn does move there the note says what it is rather than only which model:
  `glm-5.3 (…) keeps failing; continuing this turn on Relay's hosted service (Relay Free).`

## C2 — Subagents fail over, following the parent's chain

`subagents.py` built each subagent's `Agent` with **no roles resolver**, and `_begin_failover`
refuses every move without one (it cannot know which presets are keyed). So no subagent had ever
failed over, and the card's QA line "a Flash pane/subagent fails over within Flash" could not be
driven — the earlier review said so and left it.

* `SubagentFactory` now passes `roles=self.roles` and `preset_id=` to the subagent's `Agent`, plus
  `failover` / `failover_hosted` from the pane (`failover_options()`, read at **spawn** time, so a
  `set_agent_options` that arrives while a subagent waits for a slot reaches it).
* `worker.py` gives the factory `main_agent=agent`.
* An injected provider is still never replaced: `Agent._injected_provider` refuses the swap, which is
  what keeps the test suite's `provider_factory` and a guest harness where they are.
* Second effect of the same line, worth knowing: with a resolver, a compaction a subagent triggers
  now uses the Summaries role, as the pane's does.

## C3 — A routed step whose provider is down drops back to the pane's own model

A failover was refused while a plan or vision swap was up. So a plan turn whose pinned planning
model's provider was down failed outright, with the pane's own model sitting there able to answer.

* `Agent._drop_routing` runs **before** `_begin_failover` in `_model_call`. It ends the routing
  (innermost first: a vision swap nests inside a plan one), emits
  `provider_retry {reason: "route_dropped", from_model, to_model, to_preset, step, text}` plus a
  `status`, logs `provider_route_dropped`, and the step is taken again on the pane's own model.
* `text`: `Planning model <model> (<preset>) is not answering; continuing on <pane model>
  (<preset>).`, or `Vision model …`.
* The ordinary chain starts only if the pane's own model fails too, and it starts with the dropped
  provider already in `tried`/`hosts` (`_dropped_routes`): it has just refused.
* Guards kept: a call that streamed part of an answer is never moved, nor an injected provider's, nor
  a cancelled turn. **Not** gated on the `failover` option — a return to the model the pane already
  has is not a move to somebody else's provider.
* One exception: an image turn is **not** dropped back when the pane's own model cannot read images.
  That is why the turn was routed; the pictures would only reach a model that refuses them, so the
  vision provider's failure is reported as before.
* Protocol 15.2.3 is the new section; 13.11 and 17.3 point at it.

## C4 — The hosted gateway owns the upstream retries

`gateway/proxy.py` fails a request over from one upstream to the next, and then the desktop's
transport retried the refusal six more times over that same chain: one refused turn could cost a
couple of dozen upstream requests.

* The gateway counts its attempts (`Outcome.attempts`) and `error()` puts `retried` (attempts − 1)
  in the refusal body when it is greater than zero — the existing `{"error": {code, message,
  resets_at}}` shape, one more field, so nothing else changes. One upstream means no retry and no
  mark, and the gateway's own admission refusals (`rate_limited`, `quota_exhausted`) never carry it.
* `hosted.upstream_retried(body)` reads it, and `HostedChatProvider._http_retry_delay` returns None
  for such a refusal (logged `hosted_retry_owned_by_gateway`). A `rate_limited` window the gateway
  reports still wins: that is the gateway's own door, not an upstream's.
* The gateway's retryable classification was `code == 429 or code >= 500`; it is now
  `proxy.RETRYABLE_STATUSES`, the same frozenset as `ChatProvider.HTTP_RETRY_STATUSES`, so 501 and
  505 are final on both sides and 408/409 transient on both. The gateway box runs `gateway/` and
  `remote/` only and cannot import `relay_core`, so `tests/test_gateway.py` asserts the two are equal
  instead of sharing a module.

**This half needs the gateway redeployed to take effect** (`docs/RELAY-FREE.md`, "Operating it":
`rsync gateway remote /opt/relay/`, `systemctl restart relay-gateway`). Nothing was deployed from
here. Until then a hosted refusal is retried by the client exactly as it was before, which is the old
behaviour and not a regression.

## Verification

Backend and gateway tests were run on a clean `git archive refs/heads/main` export with only this
work's hunks applied — never the shared working tree, which holds six other sessions' half-finished
edits. Log: `followon-backend-tests.log`. The GUI change is one Options row and two lines of
plumbing in `Pane.h`; build log: `followon-build.log`.
