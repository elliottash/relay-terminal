---
id: XH4K
type: work
status: needs-verification
labels: [feature, providers, routing]
assignee: claude-code
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: owner, in a Claude Code session, 2026-09-20
links: {plans: [], commits: [e8220208], evidence: [docs/qa_evidence/2026-09-20-usage-limits-picker/], related: [DC4J, YJG7], github: null}
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
**Goal.** The worker polls z.ai's and Kimi's quota endpoints for every configured GLM/Kimi preset and emits the standard `usage_limits` event with the vendor's own `resets_at`, so the picker shows live figures and an exhausted GLM/Kimi subscription is greyed and skipped until its real reset — without waiting for a 429. Owner-approved 2026-09-22 (thread note 20260920T220737Z).

**Findings.**

- The event and GUI path already exist and need no change: protocol 29.3's `usage_limits` (`{event, preset, windows: [{kind: "5h"|"weekly", used_percent, resets_at}], status?}`, `resets_at` unix seconds) is handled generically in `src/Pane.h:7010` → `noteLimits` (`src/Pane.h:1319`), which overlays `m_limits` on `modelCatalog()`, greys exhausted rows and re-sends `models/fallbacks`. Window normalisation is `guest_harness.limit_windows` (`backend/relay_core/guest_harness.py:309`), reusable for any source.
- What does not exist: any worker-side polling of vendor HTTP endpoints. Today limits arrive only from guest harnesses (`guest_harness_provider.usage_limits_event`, `guest_harness_provider.py:518`, cached in `_LAST_LIMITS` and re-attached to `presets` rows by `preset_rows()`), from Relay Free's `hosted_quota`, and from the GUI's 30-minute 429 heuristic (`src/Pane.h:1351` `markExhausted`, fired at `src/Pane.h:10304`/`:10373` "a provider with no quota endpoint (GLM, Kimi, MiniMax plans)").
- The worker's established pattern for a background fetch that must never block the protocol thread is `openrouter_catalog.py`: daemon thread, `urllib.request` with a timeout, `set_listener()` → worker pushes a fresh `presets` when it lands (`backend/worker.py` `emit_presets` + the three `set_listener` registrations).
- Keys come from `keystore.lookup(preset_id)` (env then Secret Service) — never the tracker's `.env`. Preset ids to cover: `glm` and `glm-coding` (both z.ai keys; `presets.py:194`/`:200`) and `kimi-code` (`presets.py:190`). The payg `kimi` preset (`api.moonshot.ai`) is a different service — out of scope unless the reference script shows otherwise.
- The owner's reference implementation is `/home/elliott/data/usage_tracker/check_usage.py` (outside this workspace; the executor can read it from the terminal). Thread note 20260920T220737Z records: z.ai `GET https://api.z.ai/api/monitor/usage/quota/limit`, `Authorization: Bearer <key>` → `data.limits[]`, weekly window is `unit == 6`, fields `percentage` (used) and `nextResetTime`; Kimi `GET https://api.kimi.com/coding/v1/usages` → `usage.limit` / `usage.remaining` / `usage.resetTime`.
- The sentence to correct is `docs/ARCHITECTURE.md:1883` ("a provider with no quota endpoint (a GLM, Kimi or MiniMax plan)").

**Steps.**

1. **Read the reference first.** Read `/home/elliott/data/usage_tracker/check_usage.py` and confirm exact hosts, headers, response field names and timestamp units (`nextResetTime` ms vs s; `resetTime` ISO vs epoch; whether z.ai's `unit` values include a 5-hour window worth mapping). The card's Kimi endpoint says `api.kimi.com` while the preset's base URL is `api.kimi.ai/coding/v1` (`presets.py:190`) — resolve which host the quota endpoint actually lives on before writing code.
2. **New module `backend/relay_core/provider_limits.py`**, on the `openrouter_catalog` pattern: an endpoint table (`glm`/`glm-coding` → z.ai, `kimi-code` → Kimi), a fetcher per vendor returning normalised windows through `guest_harness.limit_windows` (z.ai: `used_percent = percentage`, weekly from `unit == 6`; Kimi: `used_percent = 100 - remaining/limit*100`), a per-preset cache in the `windows/status/updated_at` shape, `set_listener()`, and `start(key_lookup=keystore.lookup, emit=…)` running a daemon thread that polls each preset that has a stored key — once at start, then every 15 min — emitting `{"event": "usage_limits", "preset": <id>, "windows": …}` only when the figures changed. Fetch failures log and keep the last figures; a preset with no key is skipped silently. Never logs key material.
3. **`backend/worker.py`**: start the poller once (next to `openrouter_catalog.start_refresh()` in `emit_presets`, guarded so it starts once per process); register its listener to re-push `presets` when the first figures land; and in `emit_presets`, add `limits: provider_limits.last(preset_id)` to the built-in rows it builds from `PRESETS` (the way `preset_rows()` does for guests), so a picker opened later still has a figure.
4. **GUI: no functional change.** The 30-min 429 heuristic in `src/Pane.h` stays as the fallback for MiniMax and for the gap before the first poll lands; update the two comments (`src/Pane.h:1349`, `:10291`) that call GLM/Kimi endpoint-less. Do not special-case presets in the pane — the vendor window with its own `resets_at` wins through the existing `exhausted()` logic as soon as a poll reports 100%.
5. **Docs.** Rewrite the `docs/ARCHITECTURE.md:1883` sentence: GLM/Kimi report through the worker's quota poll (name the endpoints and the cadence); only MiniMax keeps the 429 heuristic. Add one sentence to protocol 29.3's usage-limits paragraph: the worker also emits `usage_limits` for subscription presets polled from vendor quota endpoints — same shape, no `guest` key. No shape change otherwise.
6. **Tests — new `tests/test_provider_limits.py`** (`PYTHONPATH=backend python3 -m unittest tests.test_provider_limits`): z.ai response → weekly window with `used_percent` and `resets_at`; Kimi response → window from `limit`/`remaining`; timestamp units converted to unix seconds; unknown `unit` values dropped, not guessed; no key → no fetch, no event; fetch failure keeps last figures and emits nothing; changed figures emit once, unchanged figures do not re-emit. No network in tests — the fetchers take an injectable opener like `openrouter_catalog.start_refresh(fetcher=…)`.

**Risks / owner questions.**

- **Host and field names** for both endpoints are recorded from a thread note, not verified against the live APIs; step 1 de-risks this against the working script. If the script and the note disagree, the script wins.
- Whether z.ai's quota endpoint serves both the standard-API key (`glm`) and the Coding Plan key (`glm-coding`) is unverified — if it only answers for Coding Plan keys, `glm` drops out of the endpoint table (it is payg, so a subscription-style window may not apply at all).
- Every pane worker polls independently; at 15 min and a handful of panes that is a few requests/hour per vendor — fine, but if a vendor rate-limits the monitor endpoint the interval is the knob.

**Verify.**

- `PYTHONPATH=backend python3 -m unittest tests.test_provider_limits` (new), plus `tests.test_provider` and `tests.test_guest_harness_provider` still green.
- Live, on a restarted Relay: with `glm-coding` (or `kimi-code`) keyed and in the priority, the picker shows the vendor's left-figure and reset within a poll of worker start; Options › Models' row says "resets <when>". Exhaust the subscription (or fake a 100% window in a test build) and the row greys with the vendor's reset time and returns by itself when it passes. The QA checklist items 1–3 then hold for GLM/Kimi without the 429 path; item 4's heuristic remains for MiniMax only.
