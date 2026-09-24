---
id: 495G
type: work
status: needs-verification
labels: [feature, models, routing]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 41320c21-601c-412d-9e4e-d5b986605fca
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'Repeated sandboxed lifecycle draws favor the account with more quota per hour until reset, while explicit picks, ranks, and exhausted accounts behave as specified.', sign_off: none, effort: medium}
source: Relay guest conversation, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-495G/evidence.md], related: [RND7, KQNP, M8S2], github: null}
---
# Pace subscription-aware routing by remaining quota and reset time

## Issue
plan it out -- relay computes % remaining (5h or weekly), divided by the time left until reset (5h or weekly), as effective availability. new panes, subagents, and mode changes (high / main / flash) are routed probabilistically to equalize effective availability across subscription plans
and allow decimals of hours (one decimal place)
currently, when there is a usage exhaustion, relay just stops. so can we include automated fallback in that case in this workflow

## Done means
- A fresh 5-hour or weekly usage report produces an effective availability score of percent remaining divided by hours until that window resets. When both windows constrain an account, the lower score is its routing score.
- New pane defaults, new subagents, and High/Main/Flash mode selections use that score for a probabilistic draw among eligible models at the same user-assigned rank. Explicit model/account picks and higher ranks remain authoritative.
- Exhausted accounts receive no draw until the provider reports available capacity; stale or missing usage is neutral. Stored reset credits do not become spendable quota until the user redeems one and refreshed limits confirm it.
- Deterministic tests show the same score and draw behavior in desktop and worker paths, including expiry, stale data, zero quota, tied ranks, and account isolation.
- Time remaining is displayed with one decimal hour (such as `1.5 h`); routing retains fractional-hour precision rather than rounding before scoring.
- On a confirmed quota exhaustion, Relay marks that account unavailable until its reported reset, selects an eligible configured fallback (including another account of the same provider), and continues the turn automatically when replay is safe. No fallback spends a reset credit or changes the pane's saved account choice; if work already produced output or side effects, Relay switches for the next turn and explains why it did not replay.

## Plan
**Goal.** Spread new work across the user's eligible subscriptions at a pace that uses each plan's remaining allowance before its reset, without consuming banked reset credits.

**Findings.** Desktop lifecycle draws use `src/ModelCatalog.cpp::drawTier`; a new pane's initial model is chosen in `initialChoice`, and High/plan transitions draw in `src/PaneEvents.cpp` and `src/Pane.h`. Worker role and subagent draws use `backend/relay_core/roles.py::ordered_candidates` and `SubagentFactory.default/choose` in `backend/relay_core/subagents.py`. Both routing implementations currently use `1 + 4 * min(remaining fraction) * max(exp(-hours/24))`, which is not the requested percentage-per-hour measure. Count and expiry of unused reset credits are already tracked (#KQNP). The existing `Agent._model_call` failover catches `ProviderError` for ordinary providers, but `_begin_failover` refuses injected guest providers; it also excludes hosts already tried, which would block a second account on the same service. The guest adapter currently turns a failed harness turn into a generic `ProviderError`, so quota exhaustion needs a structured reason before it can take a distinct path.

**Score and draw.** For each fresh, applicable 5-hour or weekly window `w`, set `rate_w = max(0, 100 - used_percent_w) / max((resets_at_w - now)/3600.0, 0.25)` in percentage points per hour. If both windows apply, `score = min(rate_5h, rate_weekly)` because either can stop the next turn; otherwise use the reported window. An exhausted, unreset window makes the account ineligible, regardless of credits. Keep user ranks as hard priority: choose among the best eligible tied rank with probability `score_i / sum(score)`. Treat missing or >30-minute-old reports as neutral by giving them the median fresh peer score (or equal shares if no fresh peers). A manually redeemed credit affects routing only when a later limits report changes the window percentages. Keep the division in fractional hours using full timestamp precision (for example, 1.5 hours); show hours remaining to one decimal place (for example, `1.5 h`). A 15-minute time floor prevents a near-zero denominator from dominating a draw; apply the floor before display rounding.

**Exhaustion handoff.** Normalize Claude, Codex, and API provider quota refusals to a structured `quota_exhausted` reason with account/preset and `resets_at` when known. Persist the exhausted account's hold until that reset or a later successful limits refresh; do not infer that a banked credit has been redeemed. At the failed lifecycle draw, pick another eligible account at the same rank using the availability scores; if none, walk lower user-approved ranks and configured failover targets. For a quota refusal, exclude the specific account, not its provider host, so a second Claude or Codex login can serve. Keep host exclusion for transport failures. If the failed attempt emitted no user-visible text and ran no side-effecting tools, preserve the conversation and retry the current request on the replacement, once per candidate. If it already emitted output or ran a tool, do not replay it; switch the pane's next-turn target and explain the partial result. Respect failover-off and user restrictions on allowed models/accounts. No automatic reset-credit use.

**Steps.** 1. Put equivalent score/eligibility rules into the Qt catalog and Python role router, using the existing per-account usage snapshots and no new credential reads in the selection path. 2. Route the initial pane choice, High/Main/Flash switches, plan entry, and default subagent creation through a lifecycle draw; retain explicit selections, configured role overrides, account identity, and rank order. Make a selection once at the lifecycle boundary, then keep it stable for that pane or turn. 3. Handle quota refresh and account exhaustion before subsequent draws; ensure simultaneous launches do not all choose the same account solely because the snapshot has not yet refreshed. 4. Add structured quota detection and a separate, bounded exhaustion handoff for API and guest providers, including same-service account switching and conversation transfer. 5. Add deterministic score and draw tests for both implementations and lifecycle integration tests for pane, mode, and subagent entry points. 6. Probe with two registered accounts using synthetic usage snapshots; record selection frequencies, confirm no reset-credit consumption, and document the routing rule in the protocol or model-routing docs.

**Risks.** Percentage is a proxy for absolute token capacity when plan sizes differ; the first implementation equalizes percentage depletion, not tokens. A 5-hour and weekly window may both constrain an account, so using the smaller rate avoids routing into a depleted weekly cap. Stale reports and rapid simultaneous launches can bias choices; the neutral fallback and a small in-flight reservation or equivalent feedback need tests before shipping. A reset credit is not usable capacity until redeemed manually, so the router must not treat it as such.

**Verify.** Deterministic seeded/controlled draws in `tests/modelcatalog_test.cpp` and `tests/test_roles.py`, focused pane/subagent integration tests, simulated quota refusals for two accounts of one provider (including safe retry, partial-output stop, side-effect stop, failover-off, and no-credit-spend), then a synthetic two-account routing probe. Never redeem a live usage reset for verification.

## Execution Summary
First increment landed as `e1ddd0b2`: desktop and worker tied-rank draws now use the tighter remaining-per-hour quota window; the worker draws a fresh role choice at mode entry; the model limits line shows hours to reset to one decimal. Exhaustion handoff and lifecycle coverage remain in progress.
Quota fallback landed as `1d1cf772`, with follow-up guards in `3ca7c6ae` and `6defb831`. Confirmed quota refusals route to another allowed account, including a second Claude/Codex login on the same service; each refusing account is held until reset or refreshed limits. Safe calls retry, partial output and guest tool actions defer routing to the next turn, and Relay explains that decision. Reset credits remain read-only. Evidence: `docs/qa_evidence/2026-09-24-495G/evidence.md`.

## Tests
Python role tests: 79 passed. Qt model catalog tests: 71 passed. Full Relay build and both exact-tree land builds passed. Focused quota handoff tests passed, including two exhausted accounts and a third fallback, same-service guest login, partial output, prior guest tool, failover off, and no reset redemption. A seeded 10,000-draw synthetic probe selected two same-rank accounts 6,625:3,375 for scores 10:5. The combined Python suite ran 215 tests with one unrelated stale OpenRouter model-name assertion; see evidence file.
