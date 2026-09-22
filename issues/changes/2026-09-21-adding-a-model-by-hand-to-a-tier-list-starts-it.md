---
id: TKN7
type: work
status: needs-verification
labels: [bug, models, settings]
assignee: agent
implemented_by: glm/glm-5.3-flashx
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-21'
source: pane 2, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-hand-added-model-start-effort/], related: [], github: null}
---
# Adding a model by hand to a tier list starts it at the top reasoning level, not a default

## Issue
issue: in the "main models" option, when i added gpt 5.6 sol, it defaulted effort to ultra reasoning, it should be medium. review more broadly how those defaults are set. is there a provider default we can use?

## Planning notes
**Where the ultra comes from.** Options › Models' `+ add a model…` button (`src/RelayWindow.h:2902`) hands the new entry `entry.efforts.last()` — the model's *top* level — for every tier, including Main:

```cpp
relay::models::curation::addToTier(tier, entry.key, entry.efforts.isEmpty() ? QString() : entry.efforts.last());
```

gpt-5.6-sol is `guest:codex|gpt-5.6-sol`, whose level list is codex's own `low · medium · high · xhigh · max · ultra` (`codex debug models`, codex-cli 0.155.1), so `.last()` is `ultra`. Main's blurb says the opposite of what the button does: "New panes start on the first" — a general-purpose row, not the hardest-reasoning one.

**How the defaults are set elsewhere, for contrast.**

| Where | Main | High | Flash / Lite |
|---|---|---|---|
| `tier_list_defaults` (the "fill the lists" buttons, `backend/relay_core/presets.py:649`) | the provider's own default level — `infer_effort(style, tier extra)` for a built-in preset, the model row's `default_effort` for a guest | the model's top level (`_top_level`; `xhigh` for codex, `_guest_top_level`) | the model's lowest level (`_low_level`) |
| `+ add a model…` (the button in the report) | **top level** | **top level** | **top level** |

So the by-hand button is the only place that uses the top level for Main, and it does so for all five lists.

**Is there a provider default to use? Yes.** Codex's catalogue carries `default_reasoning_level` per model and Relay already reads it into the row as `default_effort` (`guest_harness_codex.catalog_rows`, `guest_harness_provider._CODEX_FALLBACK_MODELS`, protocol 29.3). Claude Code's rows carry `default_effort: None`. `presets.tier_list_defaults` already consumes it. The GUI does not: `relay::models::Entry` (`src/ModelCatalog.h:26`) has `efforts` but no `defaultEffort`, and `catalogFrom` (`src/ModelCatalog.cpp:155`) never reads the key. That is the missing link.

Built-in presets send no per-model `default_effort`; their provider default is the level implied by the built-in tier extra (`infer_effort`), which `catalog_rows` could also send per model so the GUI has one rule for everyone.

**The wrinkle that needs the owner.** Codex's own default for gpt-5.6-sol is **low**, not medium — read off this machine:

```
gpt-6-astra | default= medium | low medium high xhigh max ultra
gpt-reserve | default= medium | low medium high xhigh max
gpt-5.6-sol | default= low    | low medium high xhigh max ultra
gpt-5.6-terra | default= medium | low medium high xhigh max ultra
gpt-5.6-luna | default= medium | low medium high xhigh max
gpt-5.5 | default= medium | low medium high xhigh
```

So "use the provider default" and "it should be medium" disagree for this one model, and the choice is the owner's (question below).

## Decisions
**2026-09-21 — what a hand-added entry starts at.** Asked in the terminal: *"When you add a model by hand to a list, what level should the new entry start at?"* The owner chose **"The provider's own default"** — Main starts at the provider's own default for that model (codex's `default_reasoning_level`: low for gpt-5.6-sol, medium for the rest), High at the level the `defaults` buttons use, Flash and Lite at the lowest. Option "Medium for Main" (medium whatever the provider says) and "Leave it unset" were not taken.

## Plan
**Goal.** A model added to one of the five lists by hand (`Options › Models › + add a model…`) starts at the level that list should use, instead of the model's top reasoning level for all five.

**Findings.**
- `src/RelayWindow.h:2902` — the add button passes `entry.efforts.last()` for every tier. This is the only place the top level is used as a *default*.
- `src/ModelCatalog.h:26` `relay::models::Entry` has `efforts` but no default level; `catalogFrom` (`src/ModelCatalog.cpp:155`) does not read `default_effort`.
- `backend/relay_core/presets.py:649` `tier_list_defaults` (the `defaults` buttons) already has the per-list rule: Main = the provider's own default level, High = the model's top level (`_guest_top_level`: `xhigh` for codex), Flash/Lite = the lowest (`_low_level`).
- Guest model rows already carry `default_effort` (codex's `default_reasoning_level`, protocol 29.3); cloud rows carry none.

**Steps.**
1. `backend/relay_core/presets.py` — one rule in one place: `tier_start_efforts(efforts, default_effort=None, guest_id="")` → `{main, high, flash, lite}` with the same levels `tier_list_defaults` uses. `catalog_rows()` gains `default_effort` (the provider's own default, from the built-in tier extra via `infer_effort`) and `tier_effort` on every row.
2. `backend/relay_core/guest_harness_provider.py` — `guest_models()` decorates each guest model row with the same `tier_effort` (it is the single choke point for guest rows in the `presets` event, codex fallback included).
3. `src/ModelCatalog.h` / `.cpp` — `Entry::tierEffort` (`QHash<QString, QString>`), parsed from `tier_effort`; a pure, testable `models::tierStartEffort(const Entry &, const QString &tier)`.
4. `src/RelayWindow.h:2902` — the add button uses `models::tierStartEffort(entry, tier)`.
5. `docs/AGENT-SESSIONS-PROTOCOL.md` — document `tier_effort` (and `default_effort` on cloud rows) in 13.8 / 29.3.
6. Tests: `tests/test_tier_lists.py` (the rule, both producers), `tests/modelcatalog_test.cpp` (`catalogFrom` parses it; `tierStartEffort` picks per list).

**Risks.** An older worker sends no `tier_effort`: `tierStartEffort` then falls back to the model's own `defaultEffort` for Main and the first/last level for the others, so a stale worker behaves as the decision intends rather than as it does today.

**Verify.** `ctest --test-dir build -R modelcatalog` (and `modelpicker`), `python3 -m pytest tests/test_tier_lists.py tests/test_presets.py -q`, plus a live check under Xvfb that adding `guest:codex|gpt-5.6-sol` to Main lands on `low` and to High on `xhigh`.

## Execution Summary
One rule decides where a hand-added row starts, and it lives in the backend, next to the one the `defaults` buttons already use.

- `backend/relay_core/presets.py` — `tier_start_efforts(efforts, default_effort, guest_id)` → `{main, high, flash, lite}`: Main the provider's own default, High the level a plan turn uses (`xhigh`, not `ultra`, on codex), Flash and Lite the lowest. `catalog_rows()` puts `default_effort` and `tier_effort` on every cloud row.
- `backend/relay_core/guest_harness_provider.py` — `guest_models()`, the single choke point for guest rows (codex's scan *and* its fallback menu, Claude Code), decorates each with the same `tier_effort`.
- `src/ModelCatalog.{h,cpp}` — `Entry::defaultEffort` and `Entry::tierEffort`, parsed from those keys, and `models::tierStartEffort(entry, tier)`: the worker's answer, or the same rules read off the entry when an older worker sends neither.
- `src/RelayWindow.h` — `modelsSection()`'s `+ add a model…` calls `tierStartEffort(entry, tier)` instead of `entry.efforts.last()`.
- `docs/AGENT-SESSIONS-PROTOCOL.md` (13.8 and 29.3) documents the keys. A sentence for `docs/ARCHITECTURE.md`'s five-lists paragraph was written and then taken back: two other sessions hold uncommitted edits inside that same paragraph and the hunk would not merge onto main's moved tip, so it goes in when one of them lands (see the thread).

For the reported case, `guest:codex|gpt-5.6-sol` now starts Main at `low` (codex's own `default_reasoning_level`) and High at `xhigh`, never `ultra`. A level the provider states but the model does not offer is stored as no level at all — the row's `default` option — so the model's own default applies at run time.

Evidence and method: `docs/qa_evidence/2026-09-21-hand-added-model-start-effort/`.

Deliberately untouched: `src/ModelPicker.cpp`'s two `efforts.last()` uses, which are the *display* of the level a not-yet-picked model would run at after the lists and the pane's own level are both unavailable — a different question from where a new list entry starts.

## Tests
- `ctest --test-dir build -R '^modelcatalog$|^modelpicker$'` — 2/2 passed. New cases: `aHandAddedModelStartsAtTheLevelTheWorkerNamed` (the codex row: `main` `low`, `high` `xhigh`) and `withoutTheWorkersAnswerTheRowItselfDecides` (the older-worker fallback, a model with no levels, a default the model does not offer).
- `python3 -m unittest tests.test_tier_lists tests.test_presets tests.test_guest_harness_provider tests.test_guest_harness_codex` — 203 tests, OK. New: `test_tier_lists.StartEffortTests` (the rule, and every cloud row's `tier_effort` agreeing with it) and the codex fallback rows' `tier_effort` in `test_guest_harness_provider`.
- Live, the real `codex` on this machine through the worker's own path (`preset_rows` → catalogue scan): `gpt-5.6-sol default=low tier_effort={'main': 'low', 'high': 'xhigh', 'flash': 'low', 'lite': 'low'}`.
- `scripts/relay-build --target relay` — built, `main.cpp` recompiled with the new `RelayWindow.h` and the new catalog symbols.

Logs: `docs/qa_evidence/2026-09-21-hand-added-model-start-effort/`.

## QA checklist
The page itself wants eyes — the level a new row shows is drawn by code this card did not change:

1. Open Options › Models (`/models`). In **main**, click `+ add a model…`, type `sol`, pick Codex's GPT-5.6-Sol. The new row's reasoning reads **`low`** — codex's own default for that model — not `ultra`.
2. Add the same model to **high**: it reads **`xhigh`** (the level a plan turn uses on codex), and to **lite**: **`low`**.
3. Add a cloud model (say GLM-5.3) to **main**: it reads **`medium`** (the level its built-in tier extra names), to **high**: **`max`**, to **lite**: **`low`**.
4. Add a Claude Code model (which states no default) to **main**: the row reads **`default`**, and a pane running it does not change its reasoning level.
5. Change the level of any such row by hand and confirm the change sticks after a restart.
6. The two `defaults` buttons still fill the five lists as before — this change did not move them.

## Tasks

- [x] One rule in the backend: `presets.tier_start_efforts`, and `default_effort` / `tier_effort` on every cloud row <!-- t:5h -->
- [x] Guest rows carry the same `tier_effort` (`guest_harness_provider.guest_models`) <!-- t:ff -->
- [x] `Entry::defaultEffort` / `Entry::tierEffort` parsed, and `models::tierStartEffort` <!-- t:xx -->
- [x] `+ add a model…` uses it instead of the model's top level <!-- t:ap -->
- [x] Protocol and architecture docs updated <!-- t:jq -->
- [x] Tests on both sides plus a live check against the real codex; the app builds <!-- t:fp -->
- [ ] A verifier walks Options › Models by hand (the QA checklist) on a lane <!-- t:7m -->
