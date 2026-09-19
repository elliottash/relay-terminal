# Provider failover — implementer notes

Card: `issues/features/2026-09-19-fail-over-to-another-provider-when-one-keeps-fai.md` (#G9VE)
Change: `backend/relay_core/agent.py`, `backend/relay_core/roles.py`, `src/Pane.h`,
`src/RelayWindow.h`, `tests/test_failover.py` (+ protocol/architecture docs).

## What was asked

On #VMZP: "for model retries, if a model doesnt work, try another provider after 3 tries or
something", answered here with: "yes exactly: other keyed presets from the same tier
(main / flash), then Relay Free. this can be disabled in the models options page".

## What Relay now does

- After a provider has failed a step through all of its own recovery — the transport's six
  429/5xx retries (#VMZP), the stall retry — the turn continues on the next provider that
  can run it without setup: **the same tier's model (Main or Flash) on every other preset
  with a stored key, then Relay Free** (`RoleResolver.failover_candidates`, catalog order).
- Each candidate is asked once; at most two besides the pane's own (`FAILOVER_PROVIDERS`);
  never a preset without a stored key (a turn must not silently spend an unchosen key),
  never a local endpoint, never a provider already tried this turn.
- A truncated step is **not** failed over (a budget problem, not a dead provider); a stall
  is. The swap lasts for the rest of the turn: `_end_failover` restores the pane's own
  provider, config and preset before the turn's terminal event.
- Each move emits `provider_retry {reason: "failover", from_model, to_model, attempt,
  max_attempts, text}` plus a `status` (the GUI prints the text line; `remote/wire.py`
  already allow-lists the event), and logs `provider_failover`.
- Subagents and side calls never fail over (no roles resolver / injected provider).
- **Off switch:** the `failover` agent option (bool, default true, protocol 12.1),
  wired to Options › Models › "Fall over to a working provider" (`agent/failover`), applied
  to a running pane at once like the other agent options.

## Deliberately not done here

- The failover candidate resolution does a keyring lookup per keyed preset at switch time
  (rare, worker-side, the resolver caches per call).
- Usage/context accounting keeps the per-model split: `self.config` follows the swap, so
  `models_used` credits the provider that actually answered.

## Evidence

- `implementer-backend-tests.log` — `tests.test_failover tests.test_agent tests.test_roles`
  on the exact committed tree: 86 pass, including the 11 new failover tests (candidate
  order, flash-within-flash, no-key/no-candidate, one-ask-each, option off, stall vs
  truncation, resolver/injection gates, option round-trip).
- `implementer-build.log` — fresh-tree Ninja build of the exact commit incl. the
  `src/Pane.h` / `src/RelayWindow.h` changes: 435/435, exit 0.
- Full targeted suite on the exact tree: 165 tests (failover, agent, roles, provider,
  hosted, provider-local) — all pass.

## QA checklist

- [ ] A pane whose provider answers 429/5xx past its retries continues the turn on the
      next keyed preset; the transcript shows "… keeps failing; continuing this turn on …"
      and the pane's own model is back for the next turn.
- [ ] A Flash pane/subagent fails over within Flash models.
- [ ] With no other keys stored, the turn lands on Relay Free; with nothing at all, the
      original provider error is reported as before.
- [ ] Options › Models › "Fall over to a working provider" turns it off for new turns and
      for a running pane at once.
- [ ] A truncated step (output limit) still ends as before, without a provider switch.
