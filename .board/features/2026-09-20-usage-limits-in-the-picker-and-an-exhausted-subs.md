---
id: XH4K
type: work
status: needs-verification
labels: [feature, providers, routing]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: owner, in a Claude Code session, 2026-09-20
links: {plans: [], commits: [e8220208, 38f5e4ceb5c6244333aba61c015b3bcf30686005, 1304cb11e4892ed3740e5aa3e855d157a931764d], evidence: [docs/qa_evidence/2026-09-20-usage-limits-picker/, docs/qa_evidence/2026-09-23-xh4k/README.md, docs/qa_evidence/2026-09-23-xh4k/], related: [DC4J, YJG7], github: null}
---
# Usage limits reach the picker live; an exhausted subscription is greyed and skipped in the priority until it resets

## Issue
when a model subscription is exhausted, it gets grayed-out and skipped in the priority until it's restored. I think we had planned that usage % would be shown in the ctrl-alt-m dialogue. it's not showing up yet.

## Evidence
**Why the percentage was not showing.** Two gaps, both in the GUI. The pane never handled the worker's `usage_limits` event (protocol 29.3), so the catalog only saw figures when a fresh `presets` answer happened to arrive; and `relay::models::catalogFrom` read the preset row's `limits` as a bare array while the worker sends `limits: {windows, status?, updated_at}`, so even that path yielded nothing.

**What changed.**

- `src/ModelCatalog.*`: `limits` reads the worker's object (a bare array still works); `Catalog.status` carries the provider's own verdict (`rejected`); `exhaustedUntil` / `exhausted(catalog, preset, now)`: a window at `used_percent >= 100`, or `status: "rejected"`, while `resets_at` is ahead or unknown — once it has passed the preset is live again without a new report; `live()` is the shown list minus exhausted presets, and `mainDefault`, `fallback`, `fallbacks` read it (the next live entry takes the place); `resetText` is the limits line's wording, shared.
- `src/ModelPicker.cpp`: an exhausted row stays in its rank, greyed (`QPalette::Disabled`), still selectable, "left" reads `0% · resets 14:30`; the tooltip says it is skipped.
- `src/Pane.h`: `m_limits` / `m_limitStatus` overlaid on `modelCatalog()`; `usage_limits` → `noteLimits`; `hosted_quota` and a `quota_exhausted` refusal → Relay Free's one `daily` window; a 429 retried by the transport (`provider_retry {reason: "http", status: 429}`) followed by a failover or the turn's death on it → `markExhausted(preset, "rate limit", now + 30 min)` for a provider with no quota endpoint (#YJG7's shape; a stall, a 5xx or a 429 the retry cleared earns nothing). Every change redraws the box (" · exhausted" on the row), notifies `SettingsWatch` (Options › Models' status line), re-sends `models/fallbacks` to the worker when the exhausted set changed, and arms a timer for the nearest reset. `/swap` from a spent model goes to the first live one.
- `src/RelayWindow.h`: Options › Models builds from `pane->modelCatalog()` (the live figures); a priority row's detail says `exhausted · resets tue · skipped`; `applyMainDefault` keeps rank 1 itself as the new-pane default so a reset does not leave new panes on the stand-in.
- `backend/relay_core/provider.py`: the `http` retry event carries `status`, the code that was refused (documented in the protocol's `provider_retry` paragraph).
- Docs: `docs/ARCHITECTURE.md`, the "Usage limits and an exhausted subscription" paragraph.

**Verified.** `build/relay-modelcatalog-tests` 22 passed (four new: the worker's limits object, exhausted until reset, the priority skipping and coming back, reset wording); `build/relay-modelpicker-tests` 9 passed (new: the greyed, selectable row and its "left" text); `PYTHONPATH=backend python3 -m unittest tests.test_provider.HTTPTests` 5 ok. Output in the evidence folder.

## QA checklist
- [ ] With Claude Code or Codex as a preset, after one turn the picker (Ctrl+Alt+M) shows the guest's "left" figure and the limits line under the list without waiting for another `presets` answer; Options › Models' provider row says "5h 62% left, resets 14:30".
- [ ] A provider whose window is at 100% (or whose harness reports `status: rejected`) is drawn greyed in the picker with "0% · resets <when>", is still selectable, reads " · exhausted" in the model box, and its priority row says "exhausted · resets <when> · skipped".
- [ ] While it is exhausted, `/swap` and the failover chain (`models/fallbacks`, the `fallbacks` request option) skip it and the next live model takes its place; when `resets_at` passes the row comes back by itself.
- [ ] A GLM/Kimi/MiniMax turn that gets 429 through the transport's retries and then fails over (or dies) marks that provider exhausted for 30 minutes; a single 429 that the retry cleared, a stall or a 5xx does not.
- [ ] Relay Free's `quota_exhausted` refusal marks Relay Free exhausted until the reset it named.

## Plan
**Goal.** Poll the Z.AI Coding Plan and Kimi Code subscription quota endpoints and feed their windows to Relay’s existing `usage_limits` picker and routing path.

**Findings.** The owner’s working `/home/elliott/data/usage_tracker/check_usage.py` uses `GET https://api.z.ai/api/monitor/usage/quota/limit` and `GET https://api.kimi.com/coding/v1/usages` with subscription keys. Kimi model calls use `api.kimi.ai`, but its quota endpoint is on `api.kimi.com`. Z.AI uses credit/token rows with unit 3 for 5h and 6 for weekly; Kimi can return count-based `usage`/`limits` and ratio-based `usages` pools. The GUI already handles `usage_limits` and exhausted windows generically.

**Steps.** 1. Add a background 15-minute poller with safe key lookup, bounded requests, parsed windows, and a last-good cache. 2. Attach the cache to built-in preset rows, emit fresh `usage_limits` events, and feed plan windows to tied-rank weighting. 3. Update the protocol and architecture docs. 4. Run offline parser/poller/routing tests and read-only live quota probes. 5. Leave restarted-Relay visual and 100%-quota behavior to independent verification.

**Risks.** Vendor quota endpoints can change without notice. Failed polls keep the last good report and do not block turns; pay-as-you-go presets are not polled. Per-pane worker processes each poll their configured plan keys.

**Verify.** `tests.test_provider_limits`, role/guest/provider tests, an exact-tree `relay` build, live read-only probes, then a restarted UI check with an exhausted fixture.

## Done means
With a configured Z.AI Coding Plan or Kimi Code key, the worker reads the vendor’s quota windows without blocking a turn, reports usage and reset times to the picker, and refreshes them while Relay runs. A fully used window is skipped until its reset. No key, a failed fetch, or an unfamiliar response leaves routing usable and does not expose key material.

## Execution Summary
Added a background worker poll for the Z.AI Coding Plan and Kimi Code subscription endpoints, using Relay's configured keys. It normalizes 5-hour, weekly, and Kimi monthly windows, emits `usage_limits` every successful 15-minute poll to keep freshness current, and attaches the last good report to built-in preset rows. The worker's tied-rank selection now reads these plan limits. Failed fetches keep the last good report; pay-as-you-go `glm` and `kimi` are not polled. Updated protocol, architecture, and Pane comments.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_provider_limits tests.test_roles tests.test_guest_harness_provider tests.test_provider`: 228 passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_provider_limits tests.test_presets tests.test_worker_encoding`: 58 passed.
- Exact-tree `relay` build passed through `scripts/land.py`.
- Read-only live probes returned Z.AI 5h/weekly and Kimi Code 5h/weekly windows with reset times. Details: `docs/qa_evidence/2026-09-23-xh4k/README.md`.
- Visual verification in a restarted Relay and a 100% quota state remain for the independent verifier.
