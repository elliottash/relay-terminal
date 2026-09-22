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

<!-- relay:entry 20260921T141021Z-m1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 14:10
t:a6's follow-ups. `1c669027` (the picks are saved with the layout; the marker gets a gutter; the
evidence re-driven), `000ea2c4` (the high role's own test, one naming function left, a command's
sentence survives). Evidence: `docs/qa_evidence/2026-09-21-model-box-modes`, re-driven end to end.

**A pane comes back on the model it picked for its mode.** `m_modePick` lived only as long as the
pane, so a pane on "kimi-k3 · flash" came back on rank 1 of the flash list. `serializeNode` writes
`mode_picks` — `{"flash": {"preset", "model", "effort"}}`, the same three fields a tier-list entry
has — and `initRestore` reads it back; "main" is never in it, because main's model is the pane's own
and is saved as `model`. The trip out and back is two pure functions in `relay::modelrows`, so it is
stated as a test, and the reader is total: anything that is not that shape is dropped rather than
guessed at. `configure` carries the role and the role alone resolves off the tier list, so a
restored pane sends its pick straight after `configured`, keeping the conversation. A pick whose
entry has left the catalog or lost its key is dropped **silently** at the first refresh that has a
catalog to ask; an exhausted subscription is not, because a reset brings it back. The pick carries
the level it was ranked with, so a list re-ordered while Relay was shut does not quietly change the
level the pane comes back on.

**The marker has a gutter.** The mode rows carry their mark in the row's *text*, because the popup,
the combo and the phone's menu all draw the same string and only one of the three can paint a
gutter — and a bullet is wider than the two spaces that stand in for it, so the names sat about 5 px
apart. The popup takes the mark back out and draws it in a fixed-width column; a page with no marked
row (the Alt+E level box) gets no gutter and is drawn exactly where it was.
`theMarkerSitsInAGutterSoTheNamesLineUp` renders the same word with and without a marker and
compares the two images column by column.

**Three more, found by re-reading.** The restored pane's *main* row named the **flash** model:
`configure` set the pane's own model from the `configured` event's `model`, which is the model of
whatever role the pane started on — it now reads `roles.main` (13.4), which `model_changed` has done
since t:a3. `test_presets.test_role_tiers_cover_every_role` pinned the High tier to `["planning"]`
and `/high` put a second role on it; the assertion is the new truth and every Python file that reads
`ROLE_TIERS` / `ROLES` / `TIER_LABELS` runs green (161 cases). And `Pane::conciseModel` is gone —
the second function that named a model, which returned a *preset label* for a guest or Relay Free
and is how the picker came to say "Codex" for gpt-5.6-sol; its last caller now calls `modelNameFor`.

**A command's sentence survives the switch it asked for.** The one-shot e7cab7d2 gave /swap is now
`sayAndSwitch(sentence, switchNow)` / `m_switchSentence`, and /glm and /kimi go through it, so
"model: glm-5.3 · z.ai (glm)." is not replaced 100–300 ms later by a generic line that does not say
which key is being spent. `/model <name>` prints nothing of its own and `/profile` switches no
model, so neither needs it.

Shots added to the evidence: `k` (/flash comes back on the pane's own flash model, not rank 1),
`saved-mode-picks.txt` (what the layout node holds), and `l` / `l2` — quit, relaunched with no
arguments, the pane back on "kimi-k3 · flash" and its box agreeing. Tests: 16 in
`tests/modelrows_test.cpp`, 19 in `tests/filterpopup_test.cpp`,
`ctest -R "modelrows|filterpopup|modelcatalog|modelpicker|panestate"` green.

<!-- relay:entry 20260921T141434Z-z1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 14:14
All seven tasks landed; the targeted C++ and Python tests re-run green on the final tree and `build/relay` rebuilt. Card to needs-verification with its Execution Summary, Tests and QA checklist. Left for the owner: three provider labels still carry a model name ("z.ai · glm-5.3 · coding plan"), which are Options › Models' provider headings.

<!-- relay:entry 20260921T175711Z-k1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 17:57
The owner confirmed the box's second design ("yes") with four rulings, recorded in the card's Decisions and design §5.3: class headers not selectable, exhausted models hidden, the chip is the model alone, and the defaults by provider count from a ranking file he can edit. Tasks t:a8–t:a10 added; a proposal for the ranking file and for Options › Models goes back to him before t:a9/t:a10 start.

<!-- relay:entry 20260921T183023Z-m1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 18:30
"yes to all your recs." — the ranking file (Markdown), the default rules by provider count, Relay Free only with no keys, harness models by name, Options › Models down to providers, keys and profiles. Design §5.4 and §5.5. Agents start on t:a9 (worker) and t:a8 (box + dialog); t:a10 follows.

<!-- relay:entry 20260921T184601Z-cs author=codex kind=progress -->
### Codex · 2026-09-21 18:46
Owner: "claim all of these and implement them". Claimed the reported deferred-start Plan bug as codex. Scope: Pane::setAgentMode, configured handling, and worker startup/recovery only; the active model-picker work (t:a8–a10) stays with its current implementers. Original report: "bug -- in codex panes, i couldnt plan on first opening, it said no agent provider is configured. search for similar bugs". Plan: retain pre-start Plan/Build selection, apply it before queued prompts after configuration, and verify in an isolated GUI with a fake worker.

<!-- relay:entry 20260921T185500Z-r1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 18:55
**t:a9 landed: the defaults come out of a file the owner can edit.** `backend/relay_core/model-ranking.md` is a **Providers** table (`provider | kind | order` — kind is plan, harness, api, router or free; order is the tie-break, in bands 10s/20s/30s/40s/90s that are design rule 2.2's preference order) and a **Models** table (`name | classes | score | notes`), 12 provider rows and 26 model rows, one per model *name*, sorted by score descending then name. `backend/relay_core/model_ranking.py` parses it — dependency-free, cached per process, a ValueError naming the line for a row that cannot be read at all, and a `check()` that reports what parsing let through (an unknown class word, a provider the worker has never heard of, a catalog model with no row, a duplicate name). `presets.INTELLIGENCE` is a `Mapping` view over the score column and `_MAIN_GROUP_ORDER` over the order column, so nothing that reads either changed.

`tier_list_defaults` now implements §5.4: it counts the providers that can take a turn (a key, a usable guest harness, a keyed custom provider; not a local endpoint, not Relay Free) and gives none of them Relay Free's three rows, one of them one model per class, two or more two per class by score — at most one per provider per class, never the same model twice, a blank score last, ties by provider order then name. Two presets of one company are one provider, so `glm` and `glm-coding` never both list glm-5.3 and the coding plan wins. The wire shape `{"plain": {tier: [{preset, model, effort?}]}, "openrouter": {...}}` is byte-identical, so nothing in `src/` had to change.

**Three decisions worth the record.** (1) A guest's models are ranked by **name** through the same table, and the guest is offered for `high` and `main` only — those are the tiers `roles.GUEST_TIERS` lets a guest entry serve, so ranking one into flash or lite would only leave a row that never runs. Per class it takes the highest-scoring model of its own list that the file classes for that class (claude code's `opus` → claude-opus-5, 51), and falls back to the first model its list names when the file has never heard of any of them, which is what it did before. (2) `LITE_LIST_FIRST` survives, in the **openrouter variant only**: the plain lists rank lite out of the file like every other class, and "openrouter first for chores" is what pressing the other button means. (3) `claude-fable-5.1` (53) and `gpt-5.6-sol` (47) are the two highest-scoring models in no class — no built-in tier names them — so they are defaults for nothing. That is now a line in the file the owner can change, rather than a consequence of a table in the code.

Commits `274078a8` (file, parser, defaults, tests), `394b282d` (protocol 13.7, ARCHITECTURE, README), `156df982` (the tick), `1fb79d59` (a missing file says so instead of ranking nothing) and `572684e1` (`_MAIN_GROUP_ORDER` has a job again: where a keyed custom provider ties). `links.commits` on the card still needs them — that line is in the same diff hunk as another session's uncommitted `status`/`assignee` change, so it waits for them. Tests: `tests/test_model_ranking.py` is new (29 cases — the parser, the shipped file, and the 0/1/2/3-provider rules); `tests/test_presets.py` gained one case tying INTELLIGENCE and the group order to the file (38 green); `tests/test_tier_lists.py`'s `test_plain`, the codex-xhigh test and the twin test assert the new lists over the same fixtures (42 green), and one stale assertion there was fixed with them — `TIER_LABELS` went lower-case in `0dddb9ee` and `'first 2 of the Flash list'` had not followed. `test_roles`, `test_customproviders`, `test_guest_harness_provider`, `test_local_tier` and `test_openrouter_catalog` re-run green (196 cases).

<!-- relay:entry 20260921T190244Z-n1 author=claude-code kind=note -->
### Claude Code · 2026-09-21 19:02
To the session that has this card claimed as `codex` in its working tree: this card's remaining tasks are in flight. t:a9 landed (274078a8…572684e1). t:a8 (the box as classes, the cutoff and class switch in the dialog) is being implemented right now by land session `fable-box2` in src/ModelRows.*, src/FilterPopup.*, src/CurrentTextComboBox.h, src/ModelPicker.*, src/ModelCatalog.*, src/Pane.h (refreshPickers, modelBoxPicked, paneRoleRows) and src/PaneState.cpp. t:a10 (Options › Models down to providers, keys and profiles; src/RelayWindow.h modelsSection) starts when t:a8 lands. Please do not start those two; `python3 scripts/land.py who` shows the live claims. Anything else on the card is done.

<!-- relay:entry 20260921T190900Z-t8 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 19:09
**t:a8 landed: the box is classes.** `dfed0146` (code and tests) and `edb298d3` (evidence, and a wider class indent the first Xvfb run asked for). Design §5.3, and the four rulings are each a thing you can point at.

`relay::modelrows::box()` replaces `page()`/`pages()`: one section per class in `Context::classes` (high, main, flash, local; `lite` is skipped outright), each a **header row** — `header=true`, `enabled=false`, `data="class:<id>"` — then that class's list in list order, one row per model, down to `curation::boxCutoff(class)`. `curation::boxShown(class)` false and the class is not drawn; nothing left to draw and it is not drawn either. "exhausted models dont show up" is implemented as *dropping* a group whose `spent()` is true, which is deliberately the opposite of the dialog and the tier lists — a list is an order the user wrote down and the dialog shows it whole, while the box answers "what can I run right now". The one row that survives the cutoff whatever its rank is the pane's own model for its class, because the highlight needs a home. `collapsedText` is now the model alone on every mode and `collapsedTooltip` carries the class.

`FilterPopup` lost the page ring and gained sections: `FilterRow::group`/`header`, a header kept or dropped by `groupHasMatch` rather than by its own words, `expandCurrent`/`onExpandKey` on Left and Right (the caller hands back a new list through `setRows` and the popup puts the highlight back on the row it was on, by `data`), and a class indent where the marker gutter was. `CurrentTextComboBox` takes `onRows`/`onExpandKey`/`replaceRows` in place of `onPages`/`pageId`/`onPageTurned`. The expansion is `Pane::m_boxExpanded` — the pane's, cleared by `openModelBox`, never a setting.

**Storage:** `models/box/<tier>` (the cutoff rank) and `models/box_off/<tier>` (the switch), absent meaning "two, on"; written through to the current profile exactly as `setTierList` is, carried by save/apply/rename/delete, and exported/imported in a profile's new `box` object (`ProfileDoc::box`, `BoxSetting{cutoff, shown}`). In the dialog each row of a class tab has a "show in box" tick that is a **cutoff** — ticking rank n ticks 1..n, unticking n unticks n and below, unticking rank 1 switches the class off, which is the same statement the tab's "show this class in the box" switch makes — and both call `onListsChanged`.

**For the t:a10 session:** the two "fill the lists" buttons are in the dialog now, as "fill from defaults" and "…with openrouter", through `ModelPicker::Context::fillFromDefaults`, a `std::function<bool(bool withOpenrouter)>`. `Pane::openModelPicker` points it at **`Pane::fillTierListsFromDefaults(bool)`**, which is the one copy of that action: `RelayWindow::modelsSection`'s `models.tier.defaults` row can call the same method and then be deleted with the rest of the lists. Nothing else in `modelsSection` was touched.

Evidence: `docs/qa_evidence/2026-09-21-model-box-classes` — thirteen steps under Xvfb on an isolated HOME with three fake keys, four main entries against a cutoff of two, and an OpenAI row ranked in high with no key so the "not shown" rule is live rather than only asserted. `stored-box-keys.txt` is what the two controls wrote. Exhaustion itself needs a provider to report a spent window, which a fake key cannot do, so that half is in `tests/modelrows_test.cpp::spentAndKeylessModelsAreNotInTheBox`; NOTES.md says so.

Tests: `ctest -R "modelrows|filterpopup|modelpicker|modelcatalog|panestate"` 5/5, with eleven cases in `modelrows`, five in `filterpopup` (one of them rendered and measured, for the indent), three in `modelpicker` and two in `modelcatalog`.

One thing worth the owner's eye rather than a fix: switching off the class a pane is *in* leaves the box with no row for the model that pane is running, and the highlight falls to the first row of another class while the chip still names the right model. The cutoff spares the pane's own model; the class switch is "do not show me this class at all". Whether the switch should spare it too is his call.

<!-- relay:entry 20260921T191500Z-t9 author=claude-fable kind=note -->
### Claude Fable · 2026-09-21 19:15
`links.commits` and `links.evidence` for t:a8 are **not** on the card yet: that single YAML line already carries another session's uncommitted edit (`274078a8, 394b282d`, which `fable-rank` says is itself waiting on a session holding `status`/`assignee` in the same hunk), so landing my addition would have taken theirs with it. To whoever lands that line next, please add `dfed0146, edb298d3` to `links.commits` and `docs/qa_evidence/2026-09-21-model-box-classes` to `links.evidence`. The t:a8 tick itself is a separate hunk and landed.

<!-- relay:entry 20260921T193100Z-d1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 19:31

**DeepSeek is a provider.** Owner: "i added deepseek as an api option". Five commits:
`179e86c2` the preset, `d03f05fe` `check()` on duplicate orders, `3d360834` `kind`/`order`
on every `presets` row, `5b95fbc8` `grouped()` following them, `39ed6ed9` the docs.

Read off https://api-docs.deepseek.com on 2026-09-21. OpenAI-compatible at
`https://api.deepseek.com`; `/api/list-models` returns exactly `deepseek-flash`
(DeepSeek-V4.1-Flash, 1M context, 384K max output, reads images) and `deepseek-v4-pro`
(DeepSeek-V4-Pro-0813, text only), both with thinking on by default. `reasoning_effort` is
none | low | high | max with `minimal` mapped to low and `medium`/`xhigh` mapped to high, so
Relay's four levels are low→low, medium→high, high→high, max→max — the same shape as Kimi and
GLM. `deepseek-flash` is a moving alias, so its row is named **`deepseek-v4.1-flash`**, which is
what OpenRouter serves as `deepseek/deepseek-v4.1-flash`: one model, one row of the picker.
Pro's twin is `deepseek/deepseek-v4-pro`, which the OpenRouter cache lists. Tiers: main
`deepseek-v4-pro` at high, flash `deepseek-flash` at low, lite the same model with
`thinking: {"type": "disabled"}` — Relay has no "off" among its four levels, so "off" is the
tier's request, and this is the one first-party API whose three tiers need no second key.
Flash reads images and Pro does not, so an image turn steps to Flash and back as it does on
Z.AI. The key needed nothing: `RELAY_DEEPSEEK_API_KEY` and the keyring entry derive from the id.

**The provider order is the owner's, and his file is not touched here.** He is still editing
`backend/relay_core/model-ranking.md`, so nothing in these five commits writes to it. What the
code does instead is stop keeping a second copy of the order: every `presets` row now carries
`kind` and `order` straight from that file (protocol 13.2), and `grouped()` in
src/ModelCatalog.cpp sorts a fold by them. The hard-coded rule 2.2 list in `accessRank` stays
only as the fallback for a row that carries neither — an older worker — and is deliberately not
kept in step by hand. `docs/MODEL-PICKING-DESIGN.md` rule 2 stops writing the order out in prose
and points at the table for the same reason.

Because the file is one a person edits half-way through, none of this can be fatal: a provider
the file does not name is `api` at `UNKNOWN_PROVIDER_ORDER` (after everything it does name),
`check()` reports an unknown provider, a preset with no row, a model with no row and — new here —
two providers sharing an `order`, all as findings rather than as a load failure. Proved against
`main`'s copy of the file, which has no `deepseek` row at all: the row comes out `api` / 500 with
both models intact and nothing raised.

**Two rows the file still needs, for the owner to paste when he is done.** In the Models table
`deepseek-v4.1-flash` already exists as OpenRouter's row and is the same row — it may want `lite`
added to its classes, since DeepSeek now serves that tier with it — and Pro has none:

    | deepseek-v4-pro | high, main |  | `deepseek`'s main model; text only, and its Flash line is the one that reads images |

**Three tests fail until that lands**, and they are left exactly as they are rather than weakened:
`test_presets.py::test_catalog_rows_resolve_efforts_and_carry_intelligence` and
`test_model_ranking.py::test_every_catalog_model_and_every_guest_model_has_a_row` want the
`deepseek-v4-pro` row, and `test_it_parses_and_check_finds_nothing` wants the file clean.
A fourth, `test_model_ranking.py::test_every_provider_the_worker_has_is_a_row_and_nothing_else_is`,
fails on the working copy only and is nothing to do with DeepSeek: it asserts the bands ascend in
`model_ranking.KINDS` order (plan, harness, api, router, free) and his draft puts the harnesses
first at 11/12 and the plans at 21/22/23. That is his ruling to finish; when he does, either
`KINDS` follows him or the bands go back. Nobody should "fix" it by editing his file.

Everything else green: test_presets (48), test_model_ranking (31), test_tier_lists (42),
test_roles (74), test_failover (56), test_openrouter_catalog (13), test_customproviders (22),
test_guest_harness_provider (63), and `ctest -R modelcatalog` 55 cases including four new ones —
a file order that contradicts `accessRank` wins outright, a ranked entry still leads, and a pair
where one side has no number falls back. No live call: the owner has no DeepSeek key stored, so
the proof is the `presets` row built with a fake `RELAY_DEEPSEEK_API_KEY` and the socket poisoned.

<!-- relay:entry 20260921T194000Z-v1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 19:40
**t:a10 — Options › Models keeps providers, keys and profiles** (design 5.5). Landed as five
commits: `ef5417d5` the page, `b88e8282` the catalog and the dialog, `dd74b49a` a box follow-up,
`f5cbf8f5` the docs, `d8605b83` the evidence.

**Off the page.** The five tier-list groups — the numbered `models/tier/<tier>/<key>` rows, drag
reorder, ×, the level choice, "+ add a model…" — and "fill the lists" (`models.tier.defaults`,
with the `models.defaults.none` hint that only served it). And the whole "models in the picker"
checklist: the provider heading toggle, the per-model checkboxes, the `models/collapsed` fold and
the per-provider "add a model by id" box.

**Where each one now lives.** The lists, the levels, reorder, remove and add are the Ctrl+Alt+M
dialog, over the same `models/tier/<tier>` storage, so the two doors can never disagree. "fill the
lists" is its **fill from defaults** / **…with openrouter** (t:a8 had already wired
`ModelPicker::Context::fillFromDefaults` to `Pane::fillTierListsFromDefaults`). The checklist has
no successor and needs none — see the next paragraph. "add a model by id" is the last row of the
`all` tab, **+ add a model by id…** (`ModelPicker::addModelById`): pick a provider, type an id, and
an id the catalog already holds is simply selected while one it does not is stored in
`models/custom` as before. The guest permission row ("when it wants to use a tool") sat under its
guest's models; it moved up under that guest's own provider row, which is what it is about.

**`models/shown` is retired, and `shown()` is one rule.** `curation::shownKeys/isShown/setShown/
resetShown` and `collapsedProviders/isCollapsed/setCollapsed` are gone; a settings file that still
holds `models/shown` or `models/collapsed` is **ignored, not migrated**, because there is nothing
left to migrate into. `models::shown(catalog)` is now: *every usable entry, in rank order, minus an
open-ended provider's long tail* — an OpenRouter row that is not one of its tier defaults, that no
tier list names and that you did not type yourself. That exception is the only one, and it exists
because 446 live rows would be most of every list they appear in. `models::allUsable(catalog)` is
the same walk without it, and only the dialog's filter reads it: the `all` tab searches it when
something is typed and puts what `shown()` held back under **more from openrouter**, and a tier
tab does the same below "not in this list", so ctrl+enter can put a tail model into a list — which
is what makes it a listed model from then on.

**A follow-up t:a8 left.** "Show this class in the box" took a class out of Alt+M even when the
pane was *running* it, so a /high pane with high switched off opened a box with its own model
nowhere in it and nothing highlighted. Same case as design 5.3's "if the pane's own model is below
the cutoff its row is shown anyway": a switched-off class is drawn when, and only when, it is the
class the pane is in, with the pane's own row and nothing else, no expansion, and a header tooltip
that says why it is there.

**Tests.** settings `theModelsPageHasNoTierListsAndNoChecklist`,
`theModelsPageButtonOpensTheDialogOnMain`; modelcatalog `shownIsEveryUsableEntry`,
`aStoredModelsShownListIsIgnored`, `anOpenEndedProvidersLongTailIsBehindTyping`,
`theFallbackThreshold`; modelpicker `theAllTabKeepsTheLongTailBehindTyping`,
`aTierTabsFilterReachesTheTailAndCtrlEnterAddsIt`, `addAModelByIdIsTheLastRowOfTheAllTab`;
modelrows `aSwitchedOffClassKeepsThePanesOwnRow`.
`ctest -R "modelrows|modelcatalog|modelpicker|modelsettings|settings"` 7/7.

**Evidence.** `docs/qa_evidence/2026-09-21-models-page-trimmed/` — drive.sh, NOTES.md and seven
Xvfb shots, with the owner's real 446-row OpenRouter cache copied into the sandbox and fetching
switched off. The page top to bottom with no list and no checklist, the button opening the dialog
on **main**, the `all` tab without the tail, "muse-spark" typed and five rows appearing under
"more from openrouter", and "+ add a model by id…" as the last row.

<!-- relay:entry 20260921T194001Z-v2 author=claude-fable kind=note -->
### Claude Fable · 2026-09-21 19:40
**The card's `links` line is still short, and I could not land it.** The front-matter line is
inside a hunk another live session (`codex-startup`) is holding uncommitted — it is changing
`status`, `assignee` and `links` together — so touching it here would land their change with mine.
Whoever lands that hunk should carry these with it:

- `links.commits` is missing `274078a8, 394b282d, 1fb79d59, 572684e1, a63f33f1, dfed0146,
  edb298d3, 05b5ba4b` (the first two are already in their working copy), and now also t:a10's
  `ef5417d5, b88e8282, dd74b49a, f5cbf8f5, d8605b83`.
- `links.evidence` is missing `docs/qa_evidence/2026-09-21-model-box-classes`, and now also
  `docs/qa_evidence/2026-09-21-models-page-trimmed`.

Only `t:a10`'s tick is landed from this session. Every task on the card is now ticked.

<!-- relay:entry 20260921T201657Z-p1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 20:16
The owner's rulings of 2026-09-21 on the ranking file, in his words: the Models classes he arranged per vendor ("anthropic: fable for high, opus for main, sonnet for flash …"); "deepseek pro is never used … use deepseek-flash for all"; "for lite … everybody is on relay free by default, or openrouter if they want privacy"; "the worker should allow the harness for flash, and defaults should be the same across plans / apis / harnesses"; background flash jobs skip a harness and "if somebody just has a harness, the flash chores run on relay flash? … i agree", "and yes to the rule on the relay free fallbacks as well"; "i want gemini latest aliases"; a `## Levels` table ("a similar defaults file for the reasoning levels across model X class"), which he filled in; "i want the effort options in relay to be determined by the model … so xhigh shows up for codex"; "for no knob models, the effort box should be grayed out. same for relay free"; `## Provider picks` ("provider picks looks good, so we could add that for cerebras for example later on"). Two agents are on it: `fable-lvl` (worker, and landing his file) and `fable-effort` (the GUI's effort box).

<!-- relay:entry 20260921T202545Z-q1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 20:25
Owner, 2026-09-21: "the text filter isnt working -- its supposed to show all available models, not just the ones selected for the box picker … there need to be 4 steps of model availability: 1 add provider, 2 add model as available, 3 add model to priority list, 4 include model in box picker. we currently only have 1, 3, 4. and its step 2 that determines the models available in the text filter. for branded providers, all models are included by default and you can uncheck them (eg i probably want to uncheck sonnet and haiku and gpt 5.5). but then for openrouter, you have to select specific models -- and maybe there are some recommended ones by default, deepseek 4.1 and gemini 3.8 flash for example." Step 2 comes back (t:a10 had retired `models/shown`); the box filter searches step 2.

<!-- relay:entry 20260921T204836Z-w1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 20:48
**The worker half of the owner's 2026-09-21 rulings is landed** (`fable-lvl`), in five commits.

`36a7972c` — **his ranking file, unchanged.** His Providers re-numbering (harnesses first), his
Models classes per vendor, and the two tables he added and filled in: `## Provider picks` and
`## Levels`. Landed whole, as his edit, before any code read them.

`f022d3a7` — **the parser.** `Ranking.pick(provider, klass)` and `Ranking.level(name, klass)`. One
reader serves both tables: a fixed key column, then one column per class in whatever order the file
writes them (a class may be left out, the four may be re-ordered, a trailing `notes` is read by
nobody), and blank / `-` / `none` all mean "nothing said". Both tables are **optional**, so a copy
of the file without them parses and every rule falls back to code — which is what lets him add or
empty one table at a time.

`a8fd95c4` — **effort levels are the model's own.** *"i want the effort options in relay to be
determined by the model … so xhigh shows up for codex."* `EFFORT_MAP` and `effort_labels` are gone:
`EFFORT_LEVELS` is the words each endpoint takes, and the word offered is the word sent. The whole
compatibility read is `nearest_effort` — the weakest listed level that is at least as much work,
the top of the list when there is none — which reproduces every answer the old mapping table gave,
so a GUI still sending `max` to the OpenAI API gets `xhigh`. `effort_fixed` is new on every model
row and every provider row: true for a model with no knob, and for every Relay Free model, whose
gateway clamps each role whatever is asked (*"for no knob models, the effort box should be grayed
out. same for relay free"*). The Levels table now decides the level on every default-filled entry,
on a hand-added model and on a pane's own pick; the Provider picks table replaces a provider's
candidate for a class (OpenRouter's Main is `glm-5.3-flash` however the Models table classes it) and
absorbed `LITE_LIST_FIRST`. Lite is `relay-lite` for everyone while Relay Free can run, with the
other button leading on OpenRouter's own lite pick. Gemini gained `gemini-pro-latest` and
`gemini-flash-latest`, with the concrete rows left a default for nothing, and DeepSeek runs
`deepseek-flash` on all three tiers with Pro classed for nothing.

`edb4a21b` — **the harness in flash, and the Relay Free fall-through.** `GUEST_TIERS` gains
`flash`, so a `/flash` pane can run on Claude Code; `BACKGROUND_ROLES` (terminal use, summaries,
suggestions, chores, audit, loop check — derived from `ROLE_TIERS`, so a new role on either tier is
covered) skip a guest entry and take the next one, and refuse a pin onto one. On a pane whose own
model *is* a harness, with nothing usable left in the list, they run on `relay-flash` / `relay-lite`
instead of a "using main" that is no answer at all.

and the docs commit that follows these — protocol §3 (rewritten: the levels table is now what each endpoint takes), §13.2, §13.7,
§13.8 and §15.2.2; design §5.6; the ranking file's "How to edit" header, which now describes four
tables and says `order` must be unique; README; `docs/RELAY-FREE.md` and `docs/ARCHITECTURE.md`,
which named the retired symbols.

**Two things for whoever picks this up.**

1. `src/Pane.h`'s BYOK dialog keeps its own copy of the preset table, and it still reads
   `{"deepseek", "deepseek · v4 pro", …, "deepseek-v4-pro"}`. It needs to say
   `deepseek · v4.1 flash` / `deepseek-flash`, and until it does
   `test_presets.GuiMirrorTests.test_the_cpp_preset_table_matches_presets_py` fails. Two string
   literals; `fable-effort` holds that header and this session was told not to touch `src/`.
2. The Relay Free fall-through is narrower than the sentence that asked for it. It fires only where
   "using main" is not an answer — a pane whose own model is a harness — because a pane on a custom
   endpoint or a local server also has no tier defaults, and there its own model is the better
   answer: `suggestions` carries recent command output and its `ACTIONS` row promises it "stays on
   your own provider". The one line to widen is marked in `roles._relay_free_role`.

Targeted tests green: `test_model_ranking` 40, `test_presets` 51 (bar the mirror above),
`test_tier_lists` 44, `test_roles` 74, `test_failover` 56, `test_session_protocol` 35,
`test_guest_harness_provider` 63, `test_openrouter_catalog` 13, plus `test_agent`, `test_subagents`,
`test_local_tier` and every other Python test that mentions `effort`. `test_images` (2) and
`test_plan_turns` (1) fail on a clean export of `main` as well and are not this work.

<!-- relay:entry 20260921T211000Z-x1 author=claude-fable kind=progress -->
### Claude Fable (fable-avail) · 2026-09-21 21:10

**Step 2 of four is back, and the box's text filter reaches it.** The owner's correction:

> "the text filter isnt working -- its supposed to show all available models, not just the ones
> selected for the box picker. on this point -- i notice now that we lost functionality. there need
> to be 4 steps of model availability: 1 add provider, 2 add model as available, 3 add model to
> priority list, 4 include model in box picker. we currently only have 1, 3, 4. and its step 2 that
> determines the models available in the text filter. for branded providers, all models are
> included by default and you can uncheck them (eg i probably want to uncheck sonnet and haiku and
> gpt 5.5). but then for openrouter, you have to select specific models -- and maybe there are some
> recommended ones by default, deepseek 4.1 and gemini 3.8 flash for example."

Five commits, all of them on `main`:

`44e5f91a` — **the storage.** `models/available`, a list of keys, **per machine and never per
profile** (a profile is the five lists; one that hid half a provider would read as the provider
being broken). Absent — every install until the first un-tick — is the default, which is exactly
what t:a10 had written into `shown()` by hand: every model of a branded provider, and of an
open-ended one only its recommended rows, which are the ones the worker's own catalog names and so
carry a `tier`, plus anything a tier list names or `models/custom` holds. So `shown()` is unchanged
on every install today. `curation::availableKeys / isAvailable / setAvailable / resetAvailable`;
`setAvailable` snapshots today's default on the first change, as `setShown` did, and un-ticking the
last one is a reset rather than an empty catalog. Two rules that are not in the owner's sentence but
follow from it: a provider added **after** the list was written keeps the default (otherwise one
un-tick today hides every model of tomorrow's provider), and a model a tier list names is available
whatever the tick says (a rank the user wrote down that the box would not offer is a list that
lies). `shown` is every usable *available* entry; `allUsable` is unchanged; `curatable` is new — what
the `all` tab draws, so an un-ticked row stays there greyed with a box to tick again.

`08936206` — **where step 2 is edited:** the Ctrl+Alt+M dialog's `all` tab. An `available` checkbox
column covering every provider of the row (a row is one model, rule 2: un-ticking sonnet un-ticks
sonnet). The tail rows under "more from openrouter" carry the same box, un-ticked; ticking one is
"you have to select specific models". The rest of the tab is grouped **by provider**, one rule per
provider name. Nothing moves under the click: the ticks and the ink are refreshed in place, because
rebuilding from inside the `itemChanged` that delivered it would delete the item being clicked.

`b6921b4e` — **the filter.** `FilterPopup::onQueryRows` is handed the query and answers with the
rows the *filter* should search, which the popup then draws in place of its own and filters by
exactly the same rule; an empty answer, or no hook, means "the rows I already have", so the Alt+E
level box and every other list are untouched. `modelrows::filtered` is the model box's answer: every
class **whole** (the cutoff is step 4 — what the box shows at rest), plus one section `other models`
holding every available model no class lists. Enter there is `pick:main|<key>`.

`fd8df23c` — the pane and Options › Models: `onQueryRows` wired, `/model <name>` falling back from
available to every usable entry (typing a name is asking for that model), and a **models… (N of M
available)** link under every provider row that opens the `all` tab on that provider — step 1 to
step 2 in one click.

`9fcd5c62` — docs: design §5.7 with the four-step table, ARCHITECTURE and README.

**Evidence:** `docs/qa_evidence/2026-09-21-model-availability/` — `drive.sh`, `NOTES.md` and nine
shots, driven against a **clean export of the landed tree** (the shared `build/` does not compile:
another session's uncommitted `relay::boardCardIdForFile` in `Pane.h`). The Options page reads
"models… (4 of 4 available)" for kimi and **"3 of 446"** for openrouter; un-ticking
`kimi-for-coding-highspeed` in the dialog changes that line to "3 of 4" while you watch, and the
model then disappears from the Alt+M filter — which is the owner's sentence tested end to end.
`conf-after.txt` is the stored list read back: the default minus the one un-tick, with three of
OpenRouter's 446 rows in it.

**Tests:** `ctest -R "modelcatalog|modelpicker|modelrows|filterpopup|settings"` 8/8.
modelcatalog `shownIsEveryAvailableUsableEntry`, `unCheckingOneModelLeavesTheRestAvailable`,
`anOpenEndedProvidersRecommendedRowsAreTheDefault`, `aModelATierListNamesStaysAvailable`,
`aProviderAddedAfterTheListKeepsTheDefault`,
`unCheckingTheLastAvailableModelIsAResetNotAnEmptyCatalog`; modelpicker
`theAllTabHasAnAvailabilityColumnAndATierTabDoesNot`,
`unTickingAModelGreysItAndTakesItOutOfTheListsAndTheBox`,
`tickingATailRowUnderMoreFromOpenrouterSelectsIt`, `aModelATierListNamesStaysTicked`,
`theDialogCanOpenFilteredToOneProvider`; filterpopup
`aTypedFilterSearchesTheWiderListTheCallerHandsBack`,
`withNoHookTheFilterNarrowsTheRowsItWasGiven`; modelrows
`theFilteredBoxOpensEveryClassAndAddsOtherModels`,
`anAvailableModelInNoListComesUnderOtherModels`, `theBoxAtRestIsUnchanged`; settings
`everyProviderRowHasAModelsLinkIntoTheAllTab`.

**One thing for the owner, and it is a product call.** Availability is stored per *entry*
(`<preset>|<model>`) but the `all` tab's tick covers every provider of a row, because a row is one
model. So un-ticking `gpt-5.6-sol` un-ticks it on Codex, the OpenAI API and OpenRouter together.
That is what "i probably want to uncheck sonnet and haiku" reads as — the model, not the route —
and the storage can already say otherwise if the answer is "no, per provider": it would want a
second control (the `via` list), not a different setting.

<!-- relay:entry 20260921T211250Z-r1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 21:12
Owner, 2026-09-21: "lets build the models pane. and we can make it where, when you open relay for the first time, you have a pane at the left and models at the right. and just remove ctrl alt m, not worth the extra confusion. just ctrl m or ctrl shift m … typing it again closes the pane (or esc as you mentioned). ticks per model is OK for now." Design §5.8, task t:a11. Key: Ctrl+Shift+M alone — Relay binds one key per surface (Options is Ctrl+Shift+O, no plain twin), and plain Ctrl+M is the carriage-return code in a terminal.

<!-- relay:entry 20260921T211909Z-e1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 21:19
**The GUI half of "the effort options are determined by the model" is landed.** Three commits:
`3ba73e82` (the change), `8be86bb6` (the BYOK dialog's preset table, which the worker's
deepseek-flash row had left behind and `tests/test_presets.GuiMirrorTests` caught), `35e7bccf`
(the snap sentence and the level box's width), plus `src/Theme.cpp`'s greyed-box rule and this
evidence.

**Relay's four levels are gone, and with them every validation against them.** `Entry::efforts`
is the model's own list, in the provider's order and the provider's words, and the word goes on
the wire as it stands — the worker validates it per model. Places that assumed four and what they
read now: `Pane::efforts()` (deleted; `offeredEfforts()` is the catalog row's list, falling
back to the preset row's and then to `models::effortLadder()`), `setEffort` / `setPaneEffort`
/ `initRestore` / `rolesObject` / `tiersObject` / `session_configured` / `model_changed`
/ `effort_changed` / `startEntry`'s level, the level box and Alt+E, Alt+. / Alt+,, `/effort`
and its `/` popup row (which now spells this pane's own levels), the palette's Reasoning
submenu, the model chip's tooltip, `ModelPicker`'s level list and reasoning column, and the
roles dialog's level pickers. `effort_labels` is retired on this side too: the levels are
already the provider's words, so an old worker's map would rename them twice, and it is read by
nobody.

**Greyed, not hidden.** `Entry::effortFixed` — the worker's `effort_fixed`, derived as
`efforts.isEmpty() || hosted` from a worker that does not send it — disables the box with
`effortFixedReason()` in its tooltip ("<name> has no reasoning level", "Relay Free sets the
level for you"), and Alt+E, `/effort` and the picker's level list say the same instead of
changing anything. The pane keeps the level it had for the next model that takes one. The
stylesheet set an explicit colour on `#statusPicker`, so `setEnabled(false)` alone changed
nothing anyone could see; there is a `:disabled` rule now.

**The snap rule.** `models::nearestEffort(levels, level)`: itself when the model takes it, else
the nearest by position in `effortLadder()` (`low medium high xhigh max ultra`, codex's own
list, of which every other provider's is a subset) — the model's top when the level is above all
of them, its lowest when below, ties going up, so `xhigh` on a model that stops at `max` lands
on `max`. The sentence rides the switch's own line rather than being wiped by it 200 ms later:
"model: kimi-k3 · conversation kept · xhigh is not a level of kimi-k3 here · using max".

**Tests.** modelcatalog `theLevelsAreTheModelsOwnListInTheProvidersOrder`,
`effortFixedIsTheWorkersWordAndOtherwiseDerived`, `theWorkersEffortFixedWinsOverTheDerivation`,
`aLevelTheModelDoesNotTakeSnapsToItsNearest`, `anOldWorkersEffortLabelsAreIgnored`; modelpicker
`levelsAreTheModelsOwnListInTheProvidersWords`, `aCodexRowListsXhighAndUltra`,
`aRelayFreeRowListsNothingAndSaysWhy`, `aLevelTheRowDoesNotTakeSnapsInThePreselect`;
modelsettings `anEffortBoxListsTheModelsOwnLevelsInTheProvidersWords`,
`thePickedModelsOwnLevelsWinOverTheProviders`; filterpopup
`aShorterListOnTheNextOpenDrawsEveryRow`.
`ctest -R "modelcatalog|modelpicker|modelrows|filterpopup|panestate|modelsettings"` 6/6, and
`tests.test_presets.GuiMirrorTests` 3/3.

**Evidence.** `docs/qa_evidence/2026-09-21-effort-by-model/` — drive.sh, NOTES.md and thirteen
Xvfb shots across two runs: Alt+E on a codex pane (six levels, `xhigh` and `ultra`), on the
OpenAI row (four, `xhigh`), on kimi (three, no `medium`); the snap line; the greyed box on
Relay Free with its tooltip; `/effort` and Alt+E answering with the reason; the `/` popup
spelling the pane's own levels; and the Ctrl+Alt+M dialog's codex row.

**One thing found and not fixed here.** `relay::FilterPopup` measures its list one row short of
what it draws once the app stylesheet is on it, so the last row is clipped by a few pixels and a
list that got *shorter* between two opens scrolls past its first row (Alt+E on openai, then on
kimi, drew "high" and "max" with "low" off the top while the box held all three). The level box's
length changes with every model switch now, so this is far easier to hit than it was, but it is
`src/FilterPopup.cpp`'s own sizing and two other sessions are working that surface.
`tests/filterpopup_test.cpp :: aShorterListOnTheNextOpenDrawsEveryRow` states the expectation and
passes headlessly, because the stylesheet — the thing that makes the measurement wrong — is not on
the popup there. NOTES.md has the reproduction.

<!-- relay:entry 20260921T220116Z-f1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 22:01
Fixed the popup sizing the effort-by-model run found and left for whoever owns
`src/FilterPopup.cpp` (the entry above, 20260921T211909Z-e1). The list was measured one row short
of what it drew, so the last row was clipped against the frame and a list that got *shorter*
between two opens stayed scrolled past its first row.

**Root cause.** The popup is built as a child of the combo box it hangs from
(`CurrentTextComboBox::filterPopup`), so the app stylesheet's `QComboBox QAbstractItemView {
border: 1px solid @border; padding: 4px; }` (src/Theme.cpp:386) matches its list as a *descendant*
of the box. `QListWidget#filterPopupList { border: none }` takes the border back; nothing takes
the padding back, and a padding on a scroll area is answered as `PM_DefaultFrameWidth`. So the
styled list keeps 8px of its own height — and `layoutForAnchor` sized the list *widget* to the sum
of the delegate's row heights, leaving the rows a viewport 8px short. The list then decided it had
to scroll, and with the current row the last one (`/effort xhigh` then `/model kimi-k3`, which
snaps to `max`) a whole row went off the top and stayed there on the next open. The popup's total
height was right all along: the old code added the list's frame to the popup but not to the list,
so those 8px sat as dead ground under the last row.

**Change.** src/FilterPopup.{h,cpp}: `PopupList::chromeHeight()` reports what the styled widget
spends on itself (frame width plus viewport margins), `layoutForAnchor` sizes the *viewport* to the
rows and the widget to that plus the chrome — measured off the polished widget, not assumed, and
re-measured from `showEvent` where real geometry exists — and the same number goes into the width.
The scroll reset became `settleScroll()`, run after the resize on every open from both
`layoutForAnchor` and `showEvent`, because a hidden widget's resize event is posted rather than
delivered; it starts at the top and scrolls only as far as the current row needs.

**Test.** `tests/filterpopup_test.cpp :: theAppStylesheetDoesNotCostTheListARow` applies
`relay::theme::applyTheme` and opens the list off a real `QComboBox#statusPicker` — which is what
reproduces it headlessly, and why the existing `aShorterListOnTheNextOpenDrawsEveryRow` could not.
Four assertions fail on the unfixed source and none on this one (`unit-before.txt`,
`unit-after.txt`); `ctest -R filterpopup` is 23/23. The test target links `relay-highlight` for
`src/Theme.cpp` — the only line outside my own files.

**Evidence.** `docs/qa_evidence/2026-09-21-popup-sizing/` — `drive.sh` runs the five steps under
Xvfb against two binaries built from clean `git archive main` exports (the checkout's `build/`
would not link: another session's `src/ModelsPane.cpp` was mid-edit), `measure.py` reads the
popup's own X geometry and counts the bands of ink in it, `measured.txt` is the whole output.
Alt+E on four levels: the last band 10px before (the descender of `xhigh` shaved — it reads
`xhiah` in `before-a-alt-e-four-zoom.png`) and 13px after. Alt+E on three after those four: **two**
bands before, `low` off the top with 27px of empty ground under `max`; three after, twice over.
Alt+M and Alt+M with `glm` typed at it: the same clearance under the last row as above the first.
Every popup is the same height before and after and 8px wider — the width the rows used to lose to
that padding.

<!-- relay:entry 20260921T220542Z-g1 author=claude-code kind=note -->
### Claude Code · 2026-09-21 22:05
Landed as `a354ef8a`. One correction to that commit's own message: its last paragraph says
`measure.py` finds the popup as "the block of changed pixels between the closed and open shots".
That was an earlier cut of the tool and it does not work — where the list hangs over the composer
strip the popup's ground *is* the strip's ground, so the diff loses the bottom of the box and the
measurement silently reads the wrong rectangle. The tool that landed asks X instead: the popup is
the one visible `relay` window that is not the main one, and `drive.sh` writes its geometry beside
each shot (`*-popup.txt`). NOTES.md and the tool's own docstring say so correctly.

<!-- relay:entry 20260921T221200Z-h1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-21 22:12
**t:a11 — the models pane.** Landed as `912dd2e2` (the pane, the key, first run, the tests) and
`<follow-up sha>` (three faults the Xvfb run found, the docs and the evidence).

The Ctrl+Alt+M modal is a `ToolPane` now — `src/ModelsPane.{h,cpp}`, `relay-modelspane` — beside
the pane it serves, hosted the way Options is, with **providers · available · priorities**. Nothing
was rewritten: available and priorities are one `relay::ModelPicker` (the modal's own guts, now a
plain `QWidget`: no `QDialog`, no `exec()`, no `pickModel()`, no "cancel") put on the flat tab or on
a class tab by this pane's tab bar, and providers is a `relay::SettingsPane` over
`modelsSection(true)` — the same section Options › Models draws, with `setEmbedded(true)` taking its
own tab row and footer away. `ModelsPane::Target` is the whole contract with the app: title, token,
catalog, model, level, mode and four callbacks, every one a `std::function`, so
`tests/modelspane_test.cpp` drives the surface with no `Pane` and no window.

`agent.model` (Ctrl+Alt+M) is **gone from the registry**, not merely unbound, per the owner: "just
remove ctrl alt m, not worth the extra confusion". `agent.modelOptions` (Ctrl+Shift+M) is the
three-state toggle — closed → open beside the active pane; open and focused → close; open and the
focus elsewhere → re-target and focus — and Escape hands the focus back and leaves it open.
`/model` alone opens it on priorities, `/models` on providers. First run (no saved layout and
`instructions/onboarded` unset) is a terminal pane at the left and this at the right, on providers.

**Three faults the driven run found**, each fixed before the shots were kept: Ctrl+Tab for the
three tabs did nothing (it is the window's Next tab and never reaches a pane — they are Alt+1/2/3
now, and ←/→ where the class row is not in front); Alt+2 typed "2" into the providers tab's search
box (an event filter on a pane never sees what its own QLineEdit swallows — they are a
`WidgetWithChildrenShortcut` as well); and the model's own name was elided to `claude-o…` at half a
window's width (the `intelligence` and `tok/s` columns are hidden while hosted; both are in every
cell's tooltip and the sort menu). A fourth came out of the first-run shot: the pane had no catalog
until its worker answered `presets`, and `rebuild()` redrew from the one it was built with —
`ModelPicker::setCatalog` and a `SettingsWatch` listener fixed that.

**Evidence:** `docs/qa_evidence/2026-09-21-models-pane/` — `drive.sh`, `NOTES.md` and fourteen
shots under Xvfb, including the first-run layout, the key closing and reopening the pane, the three
tabs, Enter changing the *served* pane's chip, Escape leaving the pane open, and both Options doors.
**Tests:** `ctest -R "modelspane|modelpicker|settings|modelrows|modelcatalog|filterpopup|panestate"`
green (`modelspane` 15 cases, `modelpicker` 37), and
`python3 -m unittest discover -s tests -p test_keybindings.py` 24/24 with a new guard that
`agent.model` is gone and `agent.modelOptions` is Ctrl+Shift+M.

<!-- relay:entry 20260921T222500Z-h2 author=claude-fable kind=note -->
### Claude Fable · 2026-09-21 22:25
The follow-up commit the entry above left as `<follow-up sha>` is **`449e3baf`** — the three faults
the driven run found, the fourth the first-run shot found, the docs (README, ARCHITECTURE,
MODEL-PICKING-DESIGN §5.8 marked built) and the evidence directory. The pane itself is `912dd2e2`.
They are **not** on the card's `links.commits`: that front-matter line sits in the same hunk as an
uncommitted `status: executing` / `assignee: codex` from another session's older copy of the card,
and landing it would have reverted the `needs-verification` / `claude-code` already on `main`. This
entry is the record instead, and the evidence is
`docs/qa_evidence/2026-09-21-models-pane/` (drive.sh, NOTES.md, fourteen shots).

t:a11 is the last of the eleven tasks and it is ticked, but the card's `status` is left as
`executing`: two other sessions are live on this card (t:a8's model box and t:a9's ranking file),
so whether #MDL1 as a whole is ready for verification is not mine to say.

<!-- relay:entry 20260921T230500Z-h3 author=claude-fable kind=evidence -->
### Claude Fable · 2026-09-21 23:05
**The quit-and-reopen round trip, driven — and the two faults it found.** `drive.sh` in
`docs/qa_evidence/2026-09-21-models-pane/` has a **run 2** now: the models pane is left on the
**available** tab, Relay is asked to quit with SIGTERM (which writes the layout), and it is started
again **with no arguments at all**, because `--workspace` is what suppresses "reopen where I left
off". `check-layout.py` makes the three assertions the shots cannot — one models node in the saved
file, carrying the tab it was left on, beside a terminal pane — and `k-before-quit.png`,
`l-after-restart.png` and `m-restored-row-switches-the-pane.png` are the before, the after and the
proof that the restored pane is serving the terminal beside it (Enter on `claude-haiku-4.5` moves
*that pane's* chip off `glm-5.3`).

It did not work the first time, and the failure was worse than a missing pane: **the whole tab was
dropped**. `relay::windowstate::isUsableNode` is the gate every saved node passes and it did not
know `"models"`; an unknown node makes its whole *split* unusable, so the terminal went with it and
Relay opened a bare window in the launch directory. One line in `src/WindowState.cpp`, and
`tests/windowstate_test.cpp` asserts the node and the split that holds it. The second fault: a
**restored pane had no "fill from defaults" buttons**, because the picker makes them in its
constructor and a restored pane is pointed at a terminal whose worker has not answered `presets`
yet — `ModelsPane::setTarget` treats gaining or losing that action as a re-target now
(`gainingFillFromDefaultsBringsItsButtons`).

**Also from the shots:** on the providers tab the **providers group opened folded**, so a first-run
window showed the blurb, the profiles and the defaults and not the one thing step 1 is about. The
fold lives under `options/collapsed/heading:providers` and defaults to folded once any provider is
set up — right for the Options page, wrong for a tab whose whole subject is providers. In the pane
the group is drawn open and has no fold control (`modelsSection(inModelsPane)`); Options is
unchanged. `a-first-run.png` is re-shot.

`ctest -R "modelspane|modelpicker|modelcatalog|modelrows|settings|filterpopup|panestate|windowstate"`
is 11/11, with `modelspane` at 16 cases.


<!-- relay:entry 20260921T232514Z-s1 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 23:25
Owner, 2026-09-21, on the models pane: "tab 1: add horizontal line dividers between providers. check the advanced provider settings. not sure whats helpful or needed. for the per-job models, i think that should be reviewed and improved and made a 4th tab. take a careful look at it to see how to improve it for that. for available, remove the recent section. i would order the sections alphabetically. it seems like i cant disable gemini flash lite. just to say -- this tab is only for terminal agents, so gemini flash lite should be optional. and relay lite shouldnt show up. tab 3: in a pane, i dont want separate tabs for the modes. they should just be in divided sections. remove the lite section." Two agents: `fable-tabs` (tabs 1–3, availability rules, the advanced dialog review) and `fable-jobs` (the per-job models as a fourth tab).

<!-- relay:entry 20260922T001500Z-j1 author=claude-fable kind=progress -->
### Claude Fable · 2026-09-22 00:15
**The per-job models, reviewed and rebuilt as the fourth tab (owner, 2026-09-21).** Landed as
`bbb4920` (the tab, its tests, the fourth tab in the models pane), `a4d2e4a` (the modal retired,
the wiring, the python guard) and `8fb5250` (two faults the driven run found). Design
`docs/MODEL-PICKING-DESIGN.md` §5.9; evidence `docs/qa_evidence/2026-09-21-models-pane-jobs/`.

**The review of `RolesDialog`, in five lines.** (1) It is a modal nobody finds, and it is the only
surface that answers "where does a summary actually go". (2) Fifteen rows in the worker's protocol
order, in Title Case, against rule 1 of this card. (3) What a job *runs on* was a grey subtitle,
blank until the worker answered, and it never said the level. (4) The override was a tier box that
re-says what the priorities lists own, plus three more boxes to reach a model — the owner's ask
("kimi k3 for main agents and glm 5.3 flash for subagents") is one pick. (5) Nothing said what may
*not* be overridden: a background role silently skips a guest harness, and the dialog offered one.

**The tab** (`src/JobsTab.{h,cpp}`, `relay-jobstab`, Alt+4) is one row per job grouped by the tier
it follows — main, high, flash, lite, local, then images and command routing — in lower-case words,
with four columns: job · what it does · **runs on** · override. "runs on" is the worker's own
`model_roles` by model name and level, and the group heading carries what the *tier* resolves to,
which is now the only place **lite** is visible: with the lite section gone from priorities there is
no lite list to edit, lite is Relay Free unless a provider's lite model was filled in, and a per-job
override is how one lite job moves. An override is one pick — Enter drops the model list over the
row's own cell, a model with levels then drops its levels, Delete or the × clears it — stored in the
retired dialog's own keys (`roles/<role>/{preset,model,effort}`, now `relay::rolestore`, which
`Pane::rolesObject` still reads), so nothing migrates. A background role is not offered a guest
harness and `setOverride` refuses one, with the reason in the row's tooltip. A write reaches the
served pane's worker at once and the column follows its next report (`Pane::onRolesResolved` →
`RelayWindow::refreshModelsPaneFor`).

**The modal is retired.** Options › Models' row is "per-job models" / `jobs…` and the palette's is
"per-job models…"; both open the pane on this tab. `src/ModelSettings.*` is the keys modal alone,
about 620 lines lighter.

**Tests.** `tests/jobstab_test.cpp` 20 cases, `tests/modelspane_test.cpp` 18 (four tabs, Alt+4, an
override reaching the served pane), `tests/modelsettings_test.cpp` 6, and `tests/test_roles.py` 76
with a new `BackgroundRoleTests` pinning `BACKGROUND_ROLES` to the set the tab derives. The Xvfb run
is against a clean `git archive main` export, because another session's uncommitted
`src/BoardPane.cpp` would not compile in the shared tree.

<!-- relay:entry 20260922T004000Z-k2 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 00:40
The owner's review of the models pane, all five notes. `7bef1f17` `f6e0d1f5` `d6e5d2b6` `cdc4b9bb`.
Evidence: `docs/qa_evidence/2026-09-21-models-pane-review` (thirteen shots, `drive.sh`, and the
profile Relay left behind). Design: `docs/MODEL-PICKING-DESIGN.md` 5.8.1.

**"tab 1: add horizontal line dividers between providers."** `SettingRow::ruleAbove` draws a 1px
line above a row in the theme's `@border`, the colour a section heading is already underlined in,
and `modelsSection` sets it on every provider row but the first's. A provider is up to three rows —
its key, its "models… (N of M available)" link and, for a guest, what it may do with a tool — and
they ran into the next provider's as one wall of text. The rule belongs to the row under it, so it
folds with the group and never leads a page.

**"check the advanced provider settings. not sure whats helpful or needed."** Retired: the dialog
(`Pane::configure`), the `agent.provider` action — gone from the keymap and from the keybinding
presets, not merely unbound — and the row that opened it. Every field had grown a better home.
Preset and API key are the providers rows themselves. Base URL and Model ID are a **custom
endpoint**, which has had its own path since 2026-09-20 ("+ add provider" → "custom endpoint…":
name, base URL, key, model ids, reasoning style); the dialog's pair could only describe one nameless
endpoint, and writing a URL into it while a named preset was selected posted that preset's key to a
foreign host, which is the HTTP 401 of 2026-09-18. Output token limit is already a row under
defaults. **Agent workspace** is deleted rather than moved: it is the pane's own directory, and one
that disagreed with the pane you are typing in is a trap. **Import keys from Warp** is a row at the
bottom of the providers. The **consent** is a sentence in the box that asks for a key, not a
checkbox: Relay has no per-action tool approvals by ruling, so a tick saying "yes, run tools" would
gate nothing and make the person agree to something they cannot decline and keep an agent.

*One field has no home and needs the worker:* **Extra request JSON**. A custom provider stores a
name, a base URL, model ids and an `effort_style`, and nothing carries a per-request body; adding
the field is a change in `backend/relay_core/customproviders.py`, which another session holds this
round. `provider/extra` is still written by `configurePreset` from the worker's own preset row, so
nothing regressed. Left for the card.

**"for available, remove the recent section. i would order the sections alphabetically."** Both. The
tab is a checklist read one provider at a time, and a block of lately-picked rows pulls those rows
out of their provider and puts them somewhere that has moved between two looks. Favorites stay at
the top — that section is one you made by hand. `models/recent` and `noteUse` stay, because `noteUse`
is what counts a model's uses for the sort menu's "usage"; nothing on the tab reads the list now.

**"i cant disable gemini flash lite … and relay lite shouldnt show up."** Two rules, both read off
the row. (1) `curation::inTerminalList` — `inAnyList` over `boxClasses()` alone, high/main/flash/
local — is what pins a model available now. The lite list does not pin: lite is not a pane mode, the
box has no lite row, and the chores read `models/tier/lite` straight (`Pane::tiersObject`), never
`shown()`, so un-ticking a lite model leaves them exactly where they were. The evidence profile is
seeded with `tier/lite = gemini-3.5-flash-lite` and `lists-after.txt` shows the tick cleared with the
list untouched. (2) `models::liteOnlyRole` — a **hosted** row whose ranking class is `lite`
(`Entry::tier`) — is skipped by `shown`, `allUsable` and `curatable`, which is every surface a
terminal agent picks from. `relay-main` and `relay-flash` are ordinary rows, and gemini-3.5-flash-
lite, gpt-5.6-luna and claude-haiku-4.5 are real models, drawn and tickable. Found with it: a
provider serving **one** model could never have that model un-ticked, because `presetNamedIn` then
found nothing for the preset and the default came back; a preset whose every model is un-ticked
leaves `"<preset>|"` behind, which is not a key and matches no entry.

**"tab 3: in a pane, i dont want separate tabs for the modes … remove the lite section."**
priorities is one scrolling page: a banded header line per class — high, main, flash, and local
where this machine serves one — over that class's numbered rows, same columns. No lite section; its
storage stays and the jobs tab is where a chore's model is set. `ModelPicker` stayed the widget
(everything the page needs was already there and none of it is about tabs); what changed is that
"which list is this" is read from the **row** (`rowTier`) and not from the tab, because four lists
are on screen at once. A class's header is a real row, not a spanned rule, because it carries a
control: the tick in the "in box" column is that class's switch, directly over the cutoff ticks it
governs. Keys: ↑/↓ across the sections stepping over headers, alt+↑/↓ inside a section and clamped
at its edge, delete and ctrl+z on the row's own class, ctrl+enter into the section the highlight is
in, ←/→ now the pane's four tabs. **"not in this list" is per section**, which is what makes
ctrl+enter's rule literally true with no fourth place and no remembered target; a model addable to
three classes appears under all three.

Looking at the shots found five things, all fixed before landing: the **model's name was elided**
("claude-opu…") while the via column had room to spare, so both columns stretch and the provider
elides first; a class header's note was elided for the same reason, so it sits in the via column;
the notes were still too long and were shortened; rank 1 of main said "new panes start here" twice
once its header said it; and the sections did not look divided, so each header carries a band
stepped off the list's own `QPalette::Base`.

Tests: `ctest -R "modelspane|modelpicker|modelcatalog|settings|appcommands|filterpopup"` green —
nine new cases in `tests/modelpicker_test.cpp`, two in `tests/modelcatalog_test.cpp`, two in
`tests/settingspane_test.cpp`, the class-tab cases in `tests/modelspane_test.cpp` ported to
sections; `tests/test_keybindings.py` 25 green.

<!-- relay:entry 20260922T005909Z-t1 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 00:59
Card freshened for a hand-over: tasks t:a12-a16 ticked, the Execution Summary carries the second half of the day with every sha and evidence path, and the QA checklist gained the models pane, availability, the effort box, the restore round trip and the ranking file. The front matter is left alone — another session holds `status`/`assignee`/`links` uncommitted there; `main` has the card at needs-verification, assignee claude-code, and the links line still wants: 274078a8 394b282d 1fb79d59 572684e1 a63f33f1 36a7972c f022d3a7 a8fd95c4 edb4a21b ae21d1f8 3ba73e82 8be86bb6 35e7bccf b6d26b22 44e5f91a 08936206 b6921b4e fd8df23c 9fcd5c62 20998265 dfed0146 edb298d3 05b5ba4b 912dd2e2 449e3baf bb5fba2b 7bef1f17 f6e0d1f5 d6e5d2b6 cdc4b9bb b249c5e6 4faa51e8 bbb49203 a4d2e4a3 8fb52503 2a660fad a354ef8a 0b9019177, and evidence 2026-09-21-model-box-classes, -model-availability, -effort-by-model, -popup-sizing, -models-pane, -models-pane-review, -models-pane-jobs. Two cards filed from open threads: #RLP7 (relay pro behind a password) and #XJSN (a custom endpoint's extra request JSON). Open for the owner: where "fable" appeared after /swap on 2026-09-21 (every record says Relay asked claude code for opus).

<!-- relay:entry 20260922T010723Z-zv author=codex kind=progress -->
### Codex · 2026-09-22 01:07
User-authorized takeover of #MDL1 verification only. The five board tools are unavailable. Preserving the existing uncommitted executing/codex/links front matter, and claiming verification through this append-only entry. Plan: reconcile all 19 QA items with final decisions, build through relay-build, drive an isolated Xvfb/config instance, record fresh evidence under docs/qa_evidence/2026-09-22-verify-MDL1/, and land only verification hunks. No delegation; CFG1, ACT1, RLP7, XJSN and other sessions' work remain outside this claim.

<!-- relay:entry 20260922T012103Z-zv author=codex kind=evidence -->
### Codex · 2026-09-22 01:21
Independent takeover verification: all 19 items reconciled with final decisions; 17 pass, 2 partial/blocked (authenticated guest turn and live paired-phone UI). Fresh evidence: docs/qa_evidence/2026-09-22-verify-MDL1/README.md. Fixed live Alt+Up/Down interception by the window in 85d0dd0f7dea0023dceaea9b6b8d4d858156805c; targeted tests, wrapper build, exact-tree build and driven reorder/new-pane regression pass. Updated only QA checklist and Verdict; preserved existing front matter and prior startup edits. Main remains needs-verification, while the shared working copy retains the earlier executing/codex claim. Board check has pre-existing MDL1 ID/task-marker/thread-order findings; read-only tests_check has no blocks but stale linked revision, so the fresh logs are the evidence. No board MCP tools or delegation; no CFG1/ACT1/RLP7/XJSN edits.

<!-- relay:entry 20260922T012338Z-za author=codex kind=progress -->
### Codex · 2026-09-22 01:23
Parent handoff: verification evidence landed in abbd41efaac21a0cdefc64f29ef0d83a72d59580; application-wide shortcut fix landed in 85d0dd0f7dea0023dceaea9b6b8d4d858156805c, with live before/after and exact-tree build. Read-only authentication readiness now confirms both installed Codex and Claude Code are signed in; no owner login setup is needed. A real Relay guest turn is still not yet verified. Current result remains 17 pass/2 partial. claude-fable-5.1 in a default chip is consistent with selecting the catalog's fable row; explicit guest:claude opus selection was driven and displays claude-opus-5 (extra/claude-name.png), so the older checklist does not require every Claude guest default to be opus.

<!-- relay:entry 20260922T013039Z-zr author=codex kind=evidence -->
### Codex · 2026-09-22 01:30
Real authenticated Codex startup/turn verified through the isolated live Relay UI: MDL1_OK, 4219ms, zero tools. No owner setup needed. Evidence: docs/qa_evidence/2026-09-22-verify-MDL1/real-guest/. The displayed/executed model is gpt-5.6-sol/low despite seeded gpt-6-astra/low; item 1 therefore remains partial pending catalog/fallback versus selection investigation. Live phone remains blocked; total stays 17 pass/2 partial. Earlier hosted fallback attempt is disclosed, not counted as guest proof. Disposable auth copy removed, no secrets committed; front matter unchanged.
