---
id: H92C
type: work
status: done
labels: [bug]
assignee: agent
implemented_by: kimi/k3
verified_by: kimi/k3
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: VisionCapabilityTests and ImageTurnTests cover the four Kimi Code ids; python3 -m pytest tests/test_images.py passes, sign_off: none, effort: low}
links: {plans: [], commits: [a6b63f205031], evidence: [], related: [], github: null}
---
# Kimi Code preset ids (k3, kimi-for-coding) read as text-only, image turns refused

## Issue
Regression of the #W56B fix: `model_supports_vision` in backend/relay_core/presets.py only knows the Moonshot platform ids `kimi-k3`/`kimi-k2.`, so the Kimi Code subscription preset's bare ids (`k3`, `k3-256k`, `kimi-for-coding`, `kimi-for-coding-highspeed`) are treated as text-only. Kimi Code's own model table (https://www.kimi.com/code/docs/en/kimi-code/models.html) says all four accept image input. With no vision fallback configured for kimi-code, an image turn is refused outright ("k3 cannot read images and no vision model is set").

> regression -- relay things kimi doesnt have an image model again
> — elliott · [session:f2a36a7f4301462489e2d546a788cddb](relay://session/f2a36a7f4301462489e2d546a788cddb) · 2026-09-25

## Done means
- `model_supports_vision` returns True for all four Kimi Code ids (`k3`, `k3-256k`, `kimi-for-coding`, `kimi-for-coding-highspeed`), per the official model table (https://www.kimi.com/code/docs/en/kimi-code/models.html: Multimodal input = Image for all four).
- `PRESETS["kimi-code"].vision` is True.
- An image turn on the kimi-code preset goes directly to `k3` (no `vision_unavailable`, no `vision_route`), proved by a test like the #W56B kimi-k3 one.
- Existing vision tests still pass, with the two assertions encoding the old "k3 unverified" behaviour updated.

## Execution Summary
Fixed in a6b63f2. Root cause: `VISION_MODELS` in `backend/relay_core/presets.py` only carried the Moonshot platform prefixes `kimi-k3`/`kimi-k2.` (the #W56B fix), while the Kimi Code subscription preset's ids are bare `k3` / `k3-256k` / `kimi-for-coding` / `kimi-for-coding-highspeed` — none matched, so `Preset.vision` was False and an image turn was refused with "k3 cannot read images and no vision model is set". Verified against Kimi Code's own model table (https://www.kimi.com/code/docs/en/kimi-code/models.html, checked 2026-09-25): Multimodal input = Image for all four ids. Added the `kimi-for-coding` and `k3` prefixes with the citation, and extended `tests/test_images.py`: `test_kimi_code_ids_read_images`, updated the two assertions that encoded the old "k3 unverified" behaviour, and a new end-to-end `test_an_image_reaches_kimi_code_k3_directly` proving an image turn on the kimi-code preset is served by `k3` with no refusal or vision swap. Evidence: `PYTHONPATH=$PWD/backend python3 -m unittest tests.test_images` — 38 tests OK. Neighbouring suites (test_presets, test_roles, test_provider_limits, test_failover) show 4 failures, but identical failures reproduce with the fix reverted: they come from other sessions' uncommitted edits in the shared tree, not this change.
