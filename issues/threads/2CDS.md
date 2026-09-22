<!-- relay:entry 20260921T105649Z-sh author=agent kind=event model=kimi-k3 pane=e73b3a18 turn=bcb327c82e3b4cada30d03f7da9b22ef/ef1fdc329d8d481f8c9708ba5dd74a63 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-21-checked-in-models-in-the-picker-but-not-offered.md

<!-- relay:entry 20260921T105706Z-2y author=agent kind=note model=kimi-k3 pane=e73b3a18 turn=bcb327c82e3b4cada30d03f7da9b22ef/ef1fdc329d8d481f8c9708ba5dd74a63 -->
### Relay · 2026-09-21 — investigation (static analysis + live config)

**Measured state** (`~/.config/RelayTerminal/relay.conf`, last written 06:49 EDT, minutes after the report):

- `models/shown` contains `guest:codex|gpt-6-astra` — astra **is** checked in the picker.
- `models/tier/main` contains `guest:codex|gpt-6-astra|medium` at rank 6 of 8 — astra **is already in the main list** now.
- The OpenAI preset has **no stored key** on this machine (`secret-tool lookup … provider openai` → empty; only glm-coding, kimi, openrouter have keys), so `openai|gpt-6-astra` is unusable: it cannot appear in "models in the picker" at all (`src/RelayWindow.h:2479` skips presets whose rows aren't usable). The only astra that can be checked is Codex's.

**Code analysis.** The main tier's "+ add a model…" offer (`src/RelayWindow.h:2882-2905`) is exactly `relay::models::shown(catalog)` minus: (a) local models, (b) guests for tiers other than main/high, (c) models **already in that tier's list**. Checklist and offer share one catalog and one `curation::isShown` (`src/ModelCatalog.cpp:248`), so a checked, usable, not-yet-listed model is *always* offered. The only way a checked model is hidden from the main add-picker is rule (c): it is already in the list — which the picker does silently, and the hint only says "A model you have not checked is not offered here", nothing about already-listed models.

**Most likely story:** astra was already in main (it is now, rank 6 — possibly marked "· skipped" if Codex's limits were exhausted, `exhaustedUntil`), the add-picker hid it by design, and nothing on screen says why.

**Adjacent defect found while tracing:** three tier-list entries are NOT in `models/shown` — `openrouter|z-ai/glm-5.3-flash` (flash), `openrouter|z-ai/glm-5.3` (high), `openrouter|google/gemini-3.5-flash-lite` (lite). Once an explicit `models/shown` list exists, `isShown` is `shown.contains(key)` only — tier membership no longer implies picker visibility, and "fill the lists"/defaults (`applyTierDefaults`) never sync `shown`. So models land in tier lists while being invisible in the picker and unofferable elsewhere. Also: the provider head toggle (`src/RelayWindow.h:2493`) calls `setShown(true)` for *every* catalog row of an open-ended provider — rechecking OpenRouter would write ~400 keys into `models/shown`.

<!-- relay:entry 20260921T105706Z-j9 author=agent kind=event model=kimi-k3 pane=e73b3a18 turn=bcb327c82e3b4cada30d03f7da9b22ef/ef1fdc329d8d481f8c9708ba5dd74a63 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260921T105713Z-c5 author=agent kind=question model=kimi-k3 pane=e73b3a18 turn=bcb327c82e3b4cada30d03f7da9b22ef/ef1fdc329d8d481f8c9708ba5dd74a63 -->
Which astra did you mean — and was it maybe already in the main list? Recommendation: treat this as a UX bug and fix the three things the trace surfaced: (1) the add-picker shows already-listed models greyed with "already in the list" instead of hiding them (or the hint says so); (2) `addToTier`/`applyTierDefaults` mark the added model shown so tier membership always implies picker visibility; (3) the open-ended provider head toggle only re-checks models that were shown before, not the whole 400-row catalog.

<!-- relay:entry 20260921T105720Z-j0 author=agent kind=event model=kimi-k3 pane=e73b3a18 turn=bcb327c82e3b4cada30d03f7da9b22ef/ef1fdc329d8d481f8c9708ba5dd74a63 -->
- ✦ agent moved this card · unchanged · Investigation done; the code paths are consistent and astra is both checked and already in the main list in the live config — need the owner to confirm which astra and what the main list showed before picking the fix.

<!-- relay:entry 20260921T105731Z-27 author=agent kind=event model=kimi-k3 pane=e73b3a18 turn=bcb327c82e3b4cada30d03f7da9b22ef/ef1fdc329d8d481f8c9708ba5dd74a63 -->
- ✦ agent updated this card · waiting_on: (unset) → owner
