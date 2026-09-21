<!-- relay:entry 20260921T112400Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 11:24
filed and claimed. Research done (names, defaults, /swap reproduced live); the design is docs/MODEL-PICKING-DESIGN.md. Subagents: one on the Alt+M / Alt+E popup, one on the catalog library next.

<!-- relay:entry 20260921T112500Z-a2 author=claude-code kind=question -->
### Claude Code · 2026-09-21 11:25
Three questions, each with the answer work is proceeding on:
1. Rank 1 of main is a guest harness (Codex, Claude Code): should a new pane start on it? Recommended: yes, because you ranked it first; the harness starts on the first turn.
2. What is Claude Code's `opus` called in the picker: `claude-opus-5` (recommended) or `opus`?
3. Relay Free's models become `relay-main`, `relay-flash`, `relay-lite` under "lowercase, no spaces". OK?

<!-- relay:entry 20260921T113632Z-d1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 11:36
The owner, on the three questions above: "i agree with your rec on all 3". A harness at rank 1 starts in a new pane (on the first turn); `opus` is named `claude-opus-5`; Relay Free's are `relay-main` / `relay-flash` / `relay-lite`. Recorded in the card's Decisions.

<!-- relay:entry 20260921T115200Z-e1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 11:52
t:a2 landed, `f3c2593a`: the catalog library half of rules 1 and 2, and rule 3's key resolution. Nothing draws anything yet.

Worker: `presets.model_name(preset_id, model_id)` — lower-case, no spaces, no vendor prefix, never on the wire. A `name` on the MODEL_CATALOG row for the three ids that cannot be derived (`kimi-code`'s `k3` -> `kimi-k3`, `claude-haiku-4-5` -> `claude-haiku-4.5`, `claude-fable-5-1` -> `claude-fable-5.1`), then GUEST_MODEL_ALIASES (`opus` -> `claude-opus-5`, per today's decision), else the id with everything up to the last `/` and a leading `~` removed. Every catalog row carries it — built-in, OpenRouter's live listing, both guests, custom providers — and `label` is the same string, so an older GUI and the phone show the name too. The prettified labels are deleted, and INTELLIGENCE is keyed by the name, so `k3` and `kimi-k3` are no longer scored 44 twice by hand.

GUI: `relay::models::nameOf`, `Entry::name` (and `label`, unchanged in type and now equal to it), `Catalog::resolveKey(preset, reported)`, `struct Group` with `grouped(catalog, rows)` / `preferred` / `spent` / `via`, and `findByName` for the next agent's `/model <name>@<provider>`. Preference order inside a row: the tier lists first, then a plan, a guest harness, a first-party API, OpenRouter, Relay Free. A plan is decided by the words of `plan` (non-empty, and not pay-as-you-go / standard api / credit), so a new "<something> plan" sorts right with nothing to edit. A local entry never joins a cloud row.

Tests: 38 in `tests/modelcatalog_test.cpp` (ten new: the design's 1.2 name table, three providers folding into one row, a tier list overriding the access order, the plan ahead of the metered API, local not folding, `preferred` skipping an exhausted and an unusable entry, `spent`, `via`, `resolveKey` alias + miss, `findByName` with and without `@`), 37 in `tests/test_presets.py` (four new: every row of every preset and a sample OpenRouter listing match `^[a-z0-9][a-z0-9._:+-]*$`, the design's table, every OPENROUTER_TWINS pair agreeing on one name bar the three serving variants, and the intelligence numbers unmoved), 276 across the six python files touched, `modelrows` / `modelpicker` / `modelsettings` green, `relay` builds (land.py's gate built the exact tree).

<!-- relay:entry 20260921T115125Z-f1 author=claude-code kind=note -->
### Claude Code · 2026-09-21 11:51
The owner added: the Switchboard agent's box reads "kimi-k3 (switchboard)" and he does not like it. The suffix is the protocol role name leaking through `modelrows::roleRowText`; folded into t:a5 (the row names the tier the role runs on, never the role's protocol name).

<!-- relay:entry 20260921T121500Z-g1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 12:15
t:a1 landed, `bb19b7b0`, evidence `docs/qa_evidence/2026-09-21-model-box-filter`.

Why nothing was highlighted, since it was not the rows and not the combo's current index: `relay::theme::applyTheme` sets the Fusion style, Fusion answers `SH_ComboBox_Popup` with 1, and Qt then draws a combo's list as a *menu* — `QComboMenuDelegate`, `CE_MenuItem` — which reads none of the stylesheet's `QComboBox … QAbstractItemView` declarations. `selection-background-color: @accent` was never consulted: the selected row was filled `#1d1613` on a `#241c18` list, a 3% step, leaving a 1px focus rectangle as the only mark. `old-popup-probe.cpp` in the evidence is that measurement (breeze `#533927` vs Fusion `#1d1613`, same stylesheet, same selection).

A colour would have been lost again at the next style change, so the list is ours: `src/FilterPopup.{h,cpp}` (`relay-filterpopup`, QtWidgets and the theme tokens, no `Pane`) is a frameless `Qt::Popup` with a visible filter line over rows it paints itself. Up/Down step over separators and switched-off rows, Enter picks, Escape closes with no change and puts the caret back in the prompt box, the row under the pointer takes the highlight, and typing filters case-insensitively and fuzzily (`relayFuzzyScore`) with the first match highlighted, the action rows matching by their own words, and a quiet "no match" line the list shrinks to.

`CurrentTextComboBox::showPopup` opens it instead of `QComboBox`'s — so Alt+M, Alt+E, the Switchboard's box and the mouse all get it — reading the rows out of the combo's own model (text, data, tooltips, separators, disabled state). Nothing that *fills* a box changed, which keeps this clear of t:a5 and t:a6: `ModelRows.cpp`, `conciseModel`, `roleRowModel` and `refreshPickers` were not touched. A pick still emits `activated(index)`, and a chord still reaches `Pane::passHotkeysThrough`'s filter, so Alt+M a second time closes the list as it has since 2026-09-20.

Tests: `ctest -R filterpopup`, 8 cases, one of which renders the popup and measures the pixels so an invisible highlight cannot come back. `manual: docs/qa_evidence/2026-09-21-model-box-filter` (eleven shots, the before shots, the probe and its output).

<!-- relay:entry 20260921T124111Z-g1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 12:41
Two decisions from the owner, recorded in the card: the box is mode rows then the mode's list ("great, left/right changes mode. add /high."), and the Ctrl+Alt+M dialog becomes "the main way to select / prioritize models". Design: docs/MODEL-PICKING-DESIGN.md section 5. Tasks t:a6 rewritten, t:a7 added.

<!-- relay:entry 20260921T125500Z-h1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 12:55
t:a3 and t:a4 landed. `9be03671` `2a60f379` (the catalog decisions and their tests), `e7cab7d2` (the pane), `2e3ff161` (evidence: `docs/qa_evidence/2026-09-21-model-defaults-and-swap`).

Rule 3 is two pure functions in `relay::models`, so the rule is one place and can be stated as tests. `startEntry(catalog, restoredPreset, restoredModel, now)` answers what a pane starts on: its own saved entry while it is still usable — through `resolveKey`, so a guest that saved `claude-opus-5` comes back as `guest:claude|opus`, and a layout with a preset and no model walks the same ladder `currentEntryKey` does — else rank 1 of the main list, guests included, carrying that entry's level. An empty answer means "this catalog cannot say", and the pane's old ladder (the saved preset, `warp_default`, the first stored key, Relay Free, a local endpoint) answers instead. `swapTarget(catalog, currentKey, rememberedKey, now)` is /swap: off rank 1 it remembers where the pane is and goes to rank 1, on rank 1 it goes back, or to rank 2 with nothing to come back to; a spent subscription is stepped over on both sides; and with no catalog it says the list is not ready rather than claiming every subscription is exhausted (design 1.4.5).

In the pane: the `presets` handler takes rank 1's **preset, model and level** (it read `provider/preset` and then that preset's *default* model, which is why picking glm-5.3-flash in one pane opened the next on glm-5.3); `initRestore` reads the saved `model` that `serializeNode` has always written; `/swap` holds a per-pane `m_swapFrom` and its sentence survives the `model_changed` that used to replace it 100–300 ms later; a mouse pick landing on rank 1 fills the same memory, so the box and the command are one toggle; `setMainModel` comes off an agent role like every other pick; `modelsCurationChanged` re-sends `tiers`, so a tier-list edit reaches a running worker instead of waiting for its next configure; and the `/profile` line says /swap, which is now true.

Two things that were quietly wrong underneath. A role's model became the pane's own key — `m_paneModel` is now the pane's model as against `m_model`, the last model the worker named for anything, so `/flash` no longer produces `openrouter|glm-5.3-flash` keys that exist in no list and no longer counts a model twice in `models/recent` and `models/uses`. And a level that merely came with a model rewrote `agent/effort` for every future pane; only an explicit level pick (Alt+E, Alt+. / Alt+,, /effort, the level box) writes it now, since Options has no row for that default.

`RelayWindow::applyMainDefault` is deleted. It copied rank 1 into `provider/preset`, `provider/model` and `agent/effort` on five of the eleven paths that change the lists, read a list of its own (`shown()`), and skipped a guest at rank 1; with the pane reading the list directly there is no second copy to keep in sync, and all six call sites already went on to `modelsCurated()`, which is what tells the panes and re-sends the failover chain. `Pane::rememberFallback` still runs, from there. The tab's helper worker (`startBoardWorker`) starts on the same `startEntry` answer, and `ranked()` no longer reads `provider/preset` — the last provider some pane switched to must not re-order every other pane's picker. `models/effort/<key>` is retired (never read or written) and a leftover `models/priority` is read only on an install that has stored no tier list.

A guest at rank 1 starts on the first turn, as the owner ruled. The worker spawns the CLI inside `configure` (backend/worker.py, 29.3), so the deferral is deferring the configure: the pane takes the preset, says "codex is this pane's agent; it starts on your first prompt", and configures when the first prompt arrives — the prompt waits in the queue, which `configured` now pumps. Every other door configures at once, because each of those is somebody asking.

Tests: eleven new cases in `tests/modelcatalog_test.cpp` for the two decisions, `ctest -R "modelcatalog|modelpicker|modelrows"` green (`modelpicker_test` now states the row order it wants instead of leaning on `provider/preset`). Evidence: `docs/qa_evidence/2026-09-21-model-defaults-and-swap` — rank 1 is seeded as a model its provider does not serve by default, so "followed the list" cannot be read as "followed the preset".

Found on the way and fixed in `4d540c0e`: since `cda3defe` this morning **no pane, helper or console could build an agent at all**. `#Z00M` bound `terminal.zoomIn` to "Ctrl++", the plus key written the way Qt writes it, and the worker's keybinding parser refused it — and the shortcut catalogue travels inside `configure`, so the refusal failed the whole request. The only trace is one line in the log; the GUI looks ordinary, because with no model reported the model box falls back to the preset's main row. The parser now accepts the trailing plus, and the test that would have caught it (the twin of the one for action ids, which exists because the same thing happened with `ssh.split_same_host` on 2026-09-18) puts every default key and every preset table's key through the worker's own parser.

<!-- relay:entry 20260921T131600Z-j1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 13:16
t:a7 landed. `b2632bf5` (the dialog and its tests), `4e14bdca` (the pane's tab and the page's button), `a61c713b` (evidence and the three fixes the shots found). Evidence: `docs/qa_evidence/2026-09-21-model-dialog-prioritize`.

Ctrl+Alt+M is now tabbed — `high · main · flash · lite · local · all`, `local` only where this machine serves one — and a tier tab **is** `curation::tierList(tier)`, not a view of it: the same storage Options › Models writes, through the same single writer (`setTierList`, which writes through to the current profile), so the two cannot disagree and there is no "apply". `Context::tier` is the mode the pane is in (`modelrows::roleTier` of its agent role), so Ctrl+Alt+M from a `/flash` pane opens on the flash list; ←/→ in the filter (empty, or the caret at that end) and Ctrl+Tab anywhere walk the tabs.

A tier row is one list **entry**, because the order between two providers of one model is exactly what a tier list expresses: the model is named once and the provider goes in a "via" column. Rank 1 of main carries "· new panes start here". A rank whose provider has no key, is exhausted, or has left the catalog altogether is greyed **in place** with the reason where the figure goes ("no key", "0% · resets 14:30", "unavailable") — a list is an order the user wrote down and dropping a rank from it silently is how the defaults stopped being answerable (design 1.3). Folding by `models::grouped` happens where order is not the point: the `all` tab and the "not in this list" section.

The edits — Alt+↑/↓, a drag, Delete with no confirmation, Ctrl+Enter at the level Options' "+ add a model…" gives (`models::tierStartEffort`, same rule about which models a list may hold), the level list writing `TierEntry::effort` with "default" as a storable state, Ctrl+Z over a stack of (tier, previous list) — each goes through `setTierList` and then `onListsChanged`, which the pane wires to `onProfileApplied`, i.e. `RelayWindow::modelsCurated()`: the page redraws, every pane re-reads and every running worker is re-sent its tiers. Typing searches `shown(catalog)`, this list's matches first. The `all` tab keeps favorites, recents and the sort menu but shows one row per group, with "via … +N" and → opening the providers beside the levels, which follow the chosen entry into its own words. The profile is in the header when there is one; a footer line spells the tab's keys; "customize…" still opens the page, and the page now opens on a "prioritize models… (Ctrl+Alt+M)" button with the live chord in it.

Looking at the first evidence run's screenshots found three things, all fixed before landing: the via list clipped its own text at 150px behind a horizontal scrollbar; the reasoning list stretched to the height of the models and read as an empty well; and → from the filter changed tab, so a folded row's providers could never be reached from the keyboard — Tab now walks filter → rows → providers → levels, the view no longer eats Tab for cell navigation, and the footer says so. A fourth, quieter one: a tab that does not hold the row you were on opens on the pane's own model where it has it rather than on whatever row happened to be first.

Tests: `ctest -R modelpicker`, 20 cases (`tests/modelpicker_test.cpp`), the old flat-layout ones rewritten onto the `all` tab rather than dropped. `ModelPick` and `pickModel()` stayed source-compatible — the fourth argument is defaulted — so nothing else moved. `relay` builds; land.py's gate built the exact tree three times.

Not in this task and still open on the card: t:a5 (names at every display site) and t:a6 (the box). The dialog reads whatever `Entry::name` holds, so t:a5's work improves it with no change here.

<!-- relay:entry 20260921T134224Z-k1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 13:42
t:a6 landed. `9d0b7b8e` (the `high` role and a pane's own pick for a role), `86ddc3b0` (the popup's
pages), `3cac1ecd` (the box), `f0dede9c` (evidence, the docs, and the fix the shots found).
Evidence: `docs/qa_evidence/2026-09-21-model-box-modes`.

The box is a stack of **pages**, one per mode (`relay::modelrows::pages`), and every page carries the
same mode rows at the top with the marker on a different one, so Left and Right move only the marker
and the list below (`FilterPopup::setPages` / `turnPage`, which keeps the filter text and re-applies
it). A mode row's parentheses name what *this pane* would run in that mode: its own pick where it
has one, else rank 1 of that tier's list, else what the worker's role summary resolved — never a
model guessed out of the whole catalog, which is why `local` with nothing ranked says the endpoint
the worker found rather than a cloud model, and why the local page offers no cloud model at all.
Below the separator are that mode's models in **list order** and one row per model
(`models::grouped`), with a right-hand "via" column naming the provider the turn would go to and
`+N` for the rest; a row whose every provider is spent or keyless is greyed **in place** with the
reason in its tooltip, which is design 1.3 and the opposite of what `build` did (it dropped them).

**Left and Right change the mode always**, whatever has been typed. The owner's words carried no
condition, and a key that means one thing with an empty filter and another with a letter in it is
the guessing this popup replaced; the filter stays editable with typing, Backspace and Ctrl+A /
Ctrl+U, so the caret lives at the end of what you typed. A list with no pages — the Alt+E level box
— is untouched and its Left and Right are still the line edit's own caret keys.

Picking a model for a **non-main** mode needed the protocol, and it was a contained change:
`set_agent_role` now takes an optional `{preset, model, effort}`, read as one tier-list entry and
resolved by the same code path (`roles.RoleResolver.resolve_entry`), so a pane's pick and a list
entry can never mean two different things. **`tiers` is not written** — the list belongs to every
other pane and to every side call — and an unusable pick falls back to the main agent with a
warning rather than erroring. Protocol 13.5 and 13.7 are at v4.6. `/high` is a real role on the High
tier, exactly as `flash` is one on Flash, with Alt+H in Alt+F's shape.

The collapsed chip is the model alone on main and `<model> · <mode>` otherwise, through
`CurrentTextComboBox::setCollapsedText` — the reason that class exists. A console's main mode is its
own main-tier worker role, which is the one line that makes its box the same list as a terminal
pane's; the shots put the two side by side and neither says "(switchboard)". The phone gets the same
strings from the same builder and a `pick:` token comes back through `modelBoxPicked`.

Looking at the first evidence run's shots found one real bug: the popup wrote the page it had been
turned to back into the box, so Escape after a turn left the *next* Alt+M opening on the wrong mode.
`pageId` is the owner's now and is re-read from the pane's mode each time the list is drawn.

Tests: `ctest -R "modelrows|filterpopup|modelcatalog|modelpicker|panestate"` green — 13 cases in
`tests/modelrows_test.cpp` (modes and their order, list order rather than alphabetical, one row per
model with via and +N, the parentheses per pane, a console building a pane's rows, the collapsed
text, a spent row greyed and a keyless one too, guests only on the main page) and 18 in
`tests/filterpopup_test.cpp` (pages open on the named page and its own current row; Left/Right turn
in place, clamp, close nothing and pick nothing; a turn keeps the filter; Enter is answered by the
row's data; Escape picks nothing; the via column rendered and measured; the one-page effort box
unchanged). Python: `tests/test_roles.py`, 77 cases, with `high` and `resolve_entry`;
`tests/test_keybindings.py` green, so Alt+H passes the worker's own catalogue check.

<!-- relay:entry 20260921T140105Z-m1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 14:01
t:a5 landed — every remaining place that prints a model prints its name, through one function, and
tier and role words are lower-case.

`Pane::modelNameFor(preset, model)` is that function for a site that holds only a preset and a
model id: the catalog through `resolveKey`, so Claude Code's `opus` and the `claude-opus-5` its
harness reports are one model and `kimi-code|k3` is `kimi-k3`, then `relay::models::nameOf` for an
id no row covers. `Pane::roleLabel` was a second, Title-Case table beside
`relay::modelrows::roleLabel`; there is one now — main, high, flash, lite, local, terminal use,
subagents, helpers, plan mode — and `roles.LABELS` and `presets.TIER_LABELS` say the same words, so
"No stored key for the flash model; using main." is the worker's wording too.

The worker sends the name it computed rather than leaving four surfaces to derive it: `model_name`
on `configured` and `model_changed`, `in_flight_model_name` on `model_changed`, `model_name` and
`from_model_name` on `model_applied`, `model_name` on every search item and `models_named` on
`session_info` (protocol section 13 and 14.3). The phone's `app/modelname.js` prefers it and
derives only for an older worker; `tests/test_web_model_name.py` runs that JavaScript under Node
and compares every answer with `presets.derived_name`, so the three copies of the rule cannot
drift.

The Sessions pane's model filter works on names: `facets.models` folds the ids history recorded
into one entry per model, so `k3` and `kimi-k3` are one line that selects both rows, and a raw id
still filters. Nothing on disk changed — `relay_model_name` is registered on the index connection.

`/model` resolves through `relay::models::findByName` first, so `/model gpt-5.6-sol` names one model
however many providers serve it and `/model gpt-5.6-sol@openrouter` says which takes it.

The first evidence run crashed Relay: `findByName` returns a pointer into the list it is handed and
the list was a temporary in an `if` condition. Fixed in `9eccaa8e` before anything else was shot.

Commits: `95773991` (the one role table), `7f8f0616` (the pane), `0dddb9ee` (the worker's
`model_name` and its lower-case words), `c5c815de` (Sessions and the ⓘ panel), `e76d6eab` (the
phone), `e955feb5` (the Actions palette), `9eccaa8e` (the crash), `6b2a3c66` + `a6a7c72e` +
`bae87fd7` (the five the grep audit found), `5de41a8f` (evidence).
Evidence: `docs/qa_evidence/2026-09-21-model-names-everywhere`, whose NOTES.md ends with the audit —
the commands, and why each remaining hit is left (Options' row headings, the Actions palette's
action names, "the Switchboard agent" as a product noun, and `conciseModel`, which only the model
box still calls).
