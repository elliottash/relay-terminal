---
id: XH4K
type: work
status: needs-verification
labels: [feature, providers, routing]
assignee: claude-code
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: 'owner, in a Claude Code session, 2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-usage-limits-picker/], related: [DC4J, YJG7], github: null}
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
