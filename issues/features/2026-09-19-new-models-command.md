---
id: Y2JW
type: work
status: ready
labels: [feature, settings]
priority: 1
rank: zzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Curate which models show in the picker (/models command)

## Issue
new /models command. it should bring to the models options. 

it should be easy to un-check models. so they dont show up in the picker.

## Investigation notes
Current state (2026-09-20):

- The model picker is a flat list built in `src/Pane.h` (~10400s): a Main row, one row per model role, tier rows, and guest rows (Claude Code, Codex). Every configured entry is always drawn — there is no per-entry "show in picker" switch anywhere (`ModelSettings.cpp`, `LocalModelsSettings.cpp` have no hide/enable concept for picker entries).
- Settings › Models exists (`RelayWindow.h` ~2027): API keys, Model roles, Default reasoning effort, Flash toggle, failover, hosted, token limit, Advanced provider. The Roles dialog is where roles/tiers map to models today, but it is several steps away and has no checkboxes.
- `/model <id>` switches models from the prompt box; no `/models` command exists yet.

Gap: nothing lets you un-check a model so it leaves the picker, and the picker's contents can only be shaped indirectly by editing roles/tiers.

## Decisions
- **Two doors, plus add** (2026-09-20): the model checklist opens via the new `/models` command *and* from a row inside the picker itself; the picker also gets an "add models" affordance — not just un-checking.
- **Per-model, flexible** (2026-09-20): curation works at the individual-model level, not just role/tier rows — e.g. Kimi K3, GLM 5.3 Flash, GPT Luna can each be in or out of the picker.
- **Global** (2026-09-20): one global shown-models list shared across all panes.

## Plan
**Goal.** A new `/models` command and a "Customize models…" row in the model picker open one checklist dialog of per-model entries; un-checking a model removes it from the picker everywhere, checking one (or adding one by id) puts it in. One global list, shared by all panes.

**Findings.**

- The picker is drawn twice from the same source: the status-bar combo `m_modelBox` in `refreshPickers()` (`src/Pane.h:10430`) — role rows (`role:<role>`, :10456), one row per stored preset (:10461), guest rows (:10480), and a "⚙ Model options…" row that opens the Roles dialog (`modelBoxPicked` :10295 → `openRolesDialog` :6062) — and the `/model` modal `agentui::pick` (`runSlashCommand`, :7428, rows at :7455-7462). Both iterate `m_stored` (id+label of each preset with a key, filled from the worker's `presets` event at :9064-9082).
- Presets are provider/plan-level (`backend/relay_core/presets.py` `PRESETS`, mirrored in `src/Pane.h:14824`); the per-provider models live in the tier table (`PROVIDER_TIERS`, three models per provider), which the GUI already holds as `m_tierCatalog` from the `tier_defaults` event (`src/Pane.h:9062`). So the checklist's model universe — each stored provider's preset model plus its tier models — is available in the GUI with **no worker changes**.
- A `set_model` request already honors an explicit `model` override (`backend/relay_core/session_protocol.py:107`), so picking a per-model entry needs only the GUI to send `{preset, model}`; the refusal path (`model_switch_refused`) already exists for a bad id.
- Slash commands are registered in `slashCommands()` (`src/Pane.h:7109-7137`, `/model` at :7116) and dispatched in `runSlashCommand` (:7426). Dialogs opened from commands follow the `/theme` precedent (:7553-7571).
- Settings › Models is built in `src/RelayWindow.h:2024-2082` with `buttonRow` helpers; `RolesDialog` in `src/ModelSettings.cpp` (`setPresets` :509) is the sibling the new dialog sits next to. Tests live in `tests/modelsettings_test.cpp` and `tests/slashcommands_test.cpp`.
- Shortcut hints: `relay::ShortcutHints` (`src/Hints.cpp`); the model picker's existing hint is `model.mouse` (`src/Pane.h:10341`).

**Entry format.** One string id per picker entry, reusing the prefixes `modelBoxPicked` already switches on: `role:<role>` (always shown, not curated), bare `<presetId>` for a provider-level row (today's rows), new `model:<presetId>:<modelId>` for a per-model row, and `guest:<id>` for Claude Code/Codex (checkable too — they're picker clutter the same way). Persisted as a `QStringList` in QSettings at `models/picker_shown`; **absent or empty means today's default (everything shown)**, so first run changes nothing and an all-unchecked list can never produce an empty picker. Unknown/stale ids are dropped at read time.

**Steps.**

1. **`PickerEntries` helper** (small, in `src/Pane.h` or a new `src/PickerEntries.h`): reads `models/picker_shown`, resolves each id against the preset mirror + `m_tierCatalog`, and returns the ordered entry list `{id, label, kind}`. Default (setting absent/empty) = the current `m_stored` + guests list, so behavior is unchanged until the user curates.
2. **Both pickers read the helper**: `refreshPickers()` (:10461) and the `/model` modal (:7456) iterate `pickerEntries()` instead of `m_stored` directly. The pane's *current* entry is always drawn (marked `current`, as today) even if hidden, so you never lose sight of what you're on. `RelayWindow.h:2746` and the panestate choices (:10965) switch to the same helper.
3. **`modelBoxPicked` learns `model:`** (:10295): a `model:<preset>:<modelId>` row calls `selectModel(preset)` extended to send the `model` override in the `set_model` request (the worker already honors it). Same for the `/model` modal's row handler (:7461).
4. **`ModelsDialog`** (new, in `src/ModelSettings.h/.cpp` beside `RolesDialog`; constructor takes the presets array + tier catalog, exactly like `RolesDialog::setPresets`): a checkable list grouped by provider — each stored provider's preset model and tier models as `model:` entries, the provider itself as a preset-level entry, guest rows at the bottom — plus an "Add model by id…" row per provider (small line-edit + Add) for ids the catalog doesn't know (the owner's GPT Luna example), which appends a `model:` entry checked. Writes `models/picker_shown` on accept; a "Reset to all" button clears the key. Has a "Roles…" button that opens the existing Roles dialog, so the old door stays reachable.
5. **`/models` command**: add `{"models", "", "Choose which models the picker shows"}` to `slashCommands()` next to `/model` (:7116); dispatch in `runSlashCommand` to a new `Pane::openModelsDialog()` (mirrors `openRolesDialog`, :6062).
6. **Picker row**: add a "✚ Customize models…" row to `m_modelBox` just above "⚙ Model options…" (:10487), handled in `modelBoxPicked` → `openModelsDialog()`. This is also the "add models" affordance: the dialog is where a hidden or new model gets checked on.
7. **Settings door**: `buttonRow` "Models in the picker…" in the Models section (`src/RelayWindow.h:2031`, after Model roles) opening the same dialog via the active pane.
8. **Refresh propagation**: after the dialog accepts, the opening pane calls `refreshPickers()`; the window asks its other panes to refresh (same hook the guest-options change uses, `src/Pane.h:752`). Other windows pick it up on their next picker rebuild — accepted, noted here.
9. **Shortcut hint** (standing rule): opening the checklist from the picker row shows `models.picker`: "Tip: /models opens this from the prompt box" — the mirror of `model.mouse` (:10341).
10. **Docs**: mention `/models` where `/model` is documented for the picker (`docs/AGENT-SESSIONS-PROTOCOL.md` ~4377 area and `docs/ARCHITECTURE.md` slash-command list :875 if built-ins are enumerated there). No protocol change — no new messages.

**Risks.**

- Hiding the model a pane is on: step 2's always-draw-current rule covers it; QA should screenshot this exact state.
- Stale ids after a preset is renamed/removed: filtered at read (step 1), never fatal.
- Free-text ids can be wrong: the provider's existing `model_switch_refused` path answers; no new validation invented.
- `m_tierCatalog` arrives with the `presets` event, so the dialog opened before the first worker hello shows preset-level rows only — acceptable; the dialog reads what's there.

**Verify.**

- Build with `scripts/relay-build`.
- New tests in `tests/modelsettings_test.cpp`: checklist round-trip (default-all, uncheck persists to `models/picker_shown`, all-unchecked falls back to default, unknown ids dropped); `tests/slashcommands_test.cpp`: `/models` is a known command and dispatches. Run `ctest --test-dir build -R 'modelsettings|slashcommands'`.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: `/models` opens the dialog; un-check one model → it leaves both the combo and the `/model` modal; add a model by id → it appears and switching to it sends `set_model {preset, model}`; the current model stays visible while hidden. Evidence (screenshots + NOTES.md) under `docs/qa_evidence/2026-09-20-models-picker-curation/`.
