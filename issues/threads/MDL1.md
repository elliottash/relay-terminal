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
