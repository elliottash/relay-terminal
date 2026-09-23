---
id: 4BPE
type: work
status: needs-verification
labels: [feature, models]
assignee: codex
implemented_by: kimi/kimi-k3
aliases: [MDL1, MDP1]
rank: k
created: '2026-09-21'
source: Claude Code in a Relay pane, 2026-09-21
links: {plans: [docs/MODEL-PICKING-DESIGN.md], commits: [bb19b7b0, b2632bf5, 4e14bdca, a61c713b, 9d0b7b8e, 86ddc3b0, 3cac1ecd, f0dede9c, 95773991, 7f8f0616, 0dddb9ee, c5c815de, e76d6eab, e955feb5, 9eccaa8e, 6b2a3c66, a6a7c72e, bae87fd7, 5de41a8f, 274078a8, 394b282d], evidence: [docs/qa_evidence/2026-09-21-model-box-filter, docs/qa_evidence/2026-09-21-model-defaults-and-swap, docs/qa_evidence/2026-09-21-model-dialog-prioritize, docs/qa_evidence/2026-09-21-model-box-modes, docs/qa_evidence/2026-09-21-model-names-everywhere, docs/qa_evidence/2026-09-22-verify-MDL1/], related: [DC4J], github: null}
---
# Model picking: one name per model, one row per model, one default

## Issue
i need to take a fresh analysis and review of model picking.

we need to organize and reconcile how model names are listed across the app. for example, the picker should say gpt-5.6-sol, not "Codex". model names should always be lowercase, no spaces.

help me think through the edge cases there.

eg, if a model comes from multiple providers (codex, openai api, openrouter), i think its better if the model only shows up once in the small picker. help me design it to work like that.

it seems like which model is selected first, and the defaults, is not clear. when i started a new pane, versus what model was selected with /swap, did not seem consistent.

/swap seems like it doesnt work.

we can use opus subagents for research and implementation

something else i want to fix in this workstream, is that when you use alt+m or alt+e, youu current selection should be highlighted. then you should be able to select with up/down arrows, and also filter with text typing (like warp's model picker). deploy a subagent to fix that

in case you didnt notice yet, i also dont like how it says "switchboard" in the picker in the switchboard agent: [a screenshot of the box reading "kimi-k3 (switchboard)"]

for the box, what do you think about this, first it just says high, main, flash, with the first model in parens, eg high (gpt-6-astra). then it lists the models for the mode you are in, in alphabetical order, with your selected model highlighted.

so it shows the models from main if a pane is on /main, or if its on /high, it shows the high models.

is this intuitive?

great, left/right changes mode. add /high.

and i realized that the model priority chooser is crtical, and currently its too hard to find -- model options, then scroll down. i think we should beef up the ctrl alt m dialogue to be the main way to select / prioritize models

## Decisions
- Owner, startup follow-up: "claim all of these and implement them".
- 2026-09-21, on the three questions below: "i agree with your rec on all 3". So: a guest harness
  at rank 1 of main is what a new pane starts on (the process starts on the first turn); Claude
  Code's `opus` is named `claude-opus-5`; Relay Free's models are `relay-main`, `relay-flash`,
  `relay-lite`.

- 2026-09-21, the box (Alt+M): mode rows first, written tier-first with the model in parentheses,
  "high (gpt-6-astra)", then the models of the mode the pane is in, the pane's own highlighted.
  Claude proposed list order rather than alphabetical, the parentheses naming what *this pane*
  would run in that mode, Left/Right changing the mode in place, and a collapsed box that is the
  model alone on main and "<model> · <mode>" otherwise; the owner: "great, left/right changes mode.
  add /high."
- 2026-09-21, the dialog (Ctrl+Alt+M): "the model priority chooser is crtical, and currently its
  too hard to find -- model options, then scroll down. i think we should beef up the ctrl alt m
  dialogue to be the main way to select / prioritize models".

- 2026-09-21, the box redesigned (owner's alternative, confirmed "yes" with four rulings): classes
  high / main / flash / local with the top-ranked models of each, Right expands a class, "more
  models…" leads to the dialog, the cutoff and a class switch live in the dialog. Rulings: "the
  class header rows are not selectable in the picker. thats redundant." — "exhausted models dont
  show up." — "no need to show the model class in the pane header" (the collapsed box is the model
  alone, always) — defaults: "without installing any providers -- you get the 3 relay models.
  default with 1 provider -- you get 1 each high / main / flash models. dont pick 2 options from the
  same provider. default with 2+ providers -- 2 each -- lets revisit how those are decided, id like
  to have a structured ranking MD or YAML in the repo i can review and edit."

- 2026-09-21, on the proposal for the ranking file, the default rules, Relay Free only with no
  keys, harness models ranked by name, and Options › Models keeping only providers, keys and
  profiles: "yes to all your recs."

## Discussion points
Three calls that were the owner's (`docs/MODEL-PICKING-DESIGN.md`, section 4). All three were
answered on 2026-09-21 — see Decisions:

1. A guest harness at rank 1 of the main list: does a new pane start it? Recommended: yes, since he
   ranked it first, with the harness process starting on the first turn.
2. The name of a Claude Code alias: `claude-opus-5` (recommended) or `opus`.
3. `relay-main` rather than `relay main`, which follows from "always lowercase, no spaces".

## Planning notes
Three read-only research passes (names, defaults, `/swap` reproduced live) are written up in
`docs/MODEL-PICKING-DESIGN.md` sections 1.1 to 1.4, with the sixteen edge cases in section 3.
`/swap` does dispatch; it toggles ranks 1 and 2 of the main list whatever the pane was on, so it
never returns to the model the owner was using, and its own sentence is overwritten by
`model_changed`.

## Plan
`docs/MODEL-PICKING-DESIGN.md`, section 5.

## Tasks
- [x] Preserve Plan/Build selection in a deferred Codex pane before its first prompt <!-- t:sp -->
- [x] Alt+M / Alt+E: current row highlighted, arrows, type to filter <!-- t:a1 -->
- [x] catalog: `models::name`, the worker's `name`, `grouped`, the pane's key resolved by name <!-- t:a2 -->
- [x] defaults: a new pane reads the main list; a pick is per pane; restore the model; re-send tiers <!-- t:a3 -->
- [x] `/swap`: a toggle with memory, a sentence that survives, a test <!-- t:a4 -->
- [x] names at every display site, lower-case roles; no protocol role name ("switchboard") in a box <!-- t:a5 -->
- [x] the box: mode rows "high (model)", Left/Right changes mode in place, that mode's list below, one row per model, `/high` <!-- t:a6 -->
- [x] the Ctrl+Alt+M dialog is where models are picked AND prioritized: tier tabs, reorder, add, remove, level, profile, "via" <!-- t:a7 -->
- [x] the box as classes: top-N per class, Right expands, headers not selectable, exhausted hidden, model alone in the chip <!-- t:a8 -->
- [x] defaults from a ranking file in the repo: 0 providers → Relay Free's three; 1 → one per class; 2+ → two per class, one per provider per class <!-- t:a9 -->
- [x] Options › Models: the tier lists and the checklist leave the page for the dialog <!-- t:aa -->
- [x] the models pane: providers / available / priorities tabs, opened by Ctrl+Shift+M beside the active pane (again closes; Esc returns focus), first run opens with it at the right; Ctrl+Alt+M and the modal go <!-- t:ab -->
- [x] the ranking file's Levels and Provider picks tables; the harness in flash with background jobs stepping past it to relay free; lite on relay free; gemini `-latest`; deepseek on flash <!-- t:ac -->
- [x] the effort box offers the model's own levels (xhigh, ultra) and greys where there is no knob or relay free clamps <!-- t:ad -->
- [x] step 2 of four: `models/available`, the tick column, the box filter searching every available model <!-- t:ae -->
- [x] the owner's review of the pane: provider dividers, the advanced dialog retired, available alphabetical with no recent, lite unpinned and relay-lite hidden, priorities as sections with no lite <!-- t:af -->
- [x] the per-job models as a fourth tab, with what each job runs on right now <!-- t:ag -->

## Execution Summary
Startup follow-up (#40SN): Plan/Build selection works before deferred guest startup, is applied before the first queued prompt, and survives configuration recovery. Verified by the isolated GUI drive in `docs/qa_evidence/2026-09-21-pane-startup-recovery/`.
Seven tasks, each by an Opus subagent in a named area of the code, each landed through
`scripts/land.py` with its own tests and an Xvfb run.

- **Names.** `presets.model_name` and `relay::models::nameOf`: lower-case, no spaces, no vendor
  prefix, never sent on the wire; every catalog row carries `name`; `Pane::modelNameFor` is the one
  call every status line, tooltip, chip, export, the Sessions list, the info panel and the phone
  use. The worker's events carry `model_name`. Role and tier words are one lower-case table.
- **One row per model.** `relay::models::grouped`: entries that share a name are one row, the
  provider chosen by the tier lists first, then plan, harness, API, OpenRouter, Relay Free; a local
  entry never joins a cloud row; a row is grey only when every provider in it is spent.
- **One default.** `startEntry`: a pane, a console and the Switchboard's worker start on rank 1 of
  the main list — its preset, model and level, a guest harness included (started on the first
  prompt). A pick in a pane is that pane's; `applyMainDefault` is deleted; a restored pane gets its
  model and its per-mode picks back; tier-list edits reach running workers.
- **`/swap`** is a toggle with memory (`swapTarget`), and its sentence survives `model_changed`
  (`sayAndSwitch`, which `/glm` and `/kimi` use too).
- **The box (Alt+M, Alt+E).** A popup that paints its own rows (the highlight had been lost to
  Fusion's menu delegate): current row highlighted, arrows, type to filter. Mode rows
  "high (model)" then the mode's list in list order; Left/Right change the mode in place; the
  collapsed box is the model alone on main, `<model> · <mode>` otherwise; the same rows in a
  console. `/high` and Alt+H; `set_agent_role` may carry one entry, per pane.
- **The dialog (Ctrl+Alt+M)** picks and prioritizes: tier tabs, reorder, add, remove, level, undo,
  the profile, "via"; Options › Models has a button to it at the top.
- Found on the way and fixed: `Ctrl++` in the keybinding catalogue stopped every `configure`
  (#Z00M's default; `4d540c0e`); the old level box hid a level behind its scrollbar.

Then the owner reviewed it and it went further (t:ac-ag):

- **The ranking file is the source.** `backend/relay_core/model-ranking.md` — four tables the owner
  edits: Providers (`provider | kind | order`, the preference order), Models (`name | classes |
  score`), Provider picks (a provider whose defaults differ; openrouter's are its own), Levels
  (`name | high | main | flash | lite`, the level a model starts at in each class). `model_ranking.py`
  parses them; `INTELLIGENCE` and the group order are views over it; `check()` reports what does not
  add up. The defaults: 0 providers → relay free's three, 1 → one per class, 2+ → two per class and
  never two from one provider.
- **Jobs.** A harness may sit in the flash list; background jobs (summaries, suggestions, chores…)
  step past a guest entry, and when a harness pane's list has nothing for them they fall through to
  relay free. Lite is relay free by default, openrouter's lite for privacy.
- **Levels are the model's own.** The effort box offers what the provider reports — codex's
  `xhigh`/`ultra`, kimi's three — and is greyed with a reason where there is no knob or relay free
  clamps. A level the next model lacks snaps to its nearest and says so once.
- **Four steps of availability** (§5.7): add provider → available → priority list → box. Step 2 is
  `models/available`, per machine, defaulting to every model of a branded provider and only the
  recommended rows of an open-ended one. The box's typed filter searches every available model and
  puts matches outside the lists under `other models`.
- **The models pane** (§5.8) replaced the modal: `Ctrl+Shift+M` opens it beside the pane it serves,
  again closes it, Escape returns focus; four tabs — providers (dividers, keys, the Warp import),
  available (alphabetical, no recent, a tick per model), priorities (one page of divided sections,
  no class tabs, no lite), jobs (every job, what it runs on right now, one override each). First run
  opens with a terminal at the left and the pane at the right. `Ctrl+Alt+M`, the modal, the roles
  dialog and the advanced provider dialog are all retired.
- Found on the way and fixed: two consoles reconfiguring their worker twice a second (#CFG1); a
  saved models pane, and a saved Activity pane, each taking their whole tab with them on reopen
  (`bb5fba2b`, #ACT1); the app stylesheet costing the popup a row (`a354ef8a`); a double click on a
  tick being read as "use this model", which is how a pane landed on gemini flash lite
  (`0b9019177`).

Commits of the second half: 36a7972c f022d3a7 a8fd95c4 edb4a21b ae21d1f8 (the file and its rules),
3ba73e82 8be86bb6 35e7bccf b6d26b22 (levels), 44e5f91a 08936206 b6921b4e fd8df23c 9fcd5c62 20998265
(availability), 912dd2e2 449e3baf bb5fba2b (the pane), 7bef1f17 f6e0d1f5 d6e5d2b6 cdc4b9bb b249c5e6
4faa51e8 (the review), bbb49203 a4d2e4a3 8fb52503 2a660fad (jobs), a354ef8a 0b9019177 (the two fixes).
Evidence: `2026-09-21-model-box-classes`, `-model-availability`, `-effort-by-model`, `-popup-sizing`,
`-models-pane`, `-models-pane-review`, `-models-pane-jobs`, `-console-configure-loop`.

## Tests
manual: docs/qa_evidence/2026-09-21-pane-startup-recovery/README.md
`ctest --test-dir build -R "modelrows|filterpopup|modelcatalog|modelpicker|panestate|conversations"`
(6/6), and `tests/test_presets.py`, `test_roles.py`, `test_keybindings.py`, `test_model_switch.py`,
`test_conv_index.py`, `test_web_model_name.py`, all run green on 2026-09-21 after the last commit.

## QA checklist
Independent verification 2026-09-22; item numbers preserve the original 19-item checklist.
Evidence and limits: `docs/qa_evidence/2026-09-22-verify-MDL1/README.md`.
Final decisions supersede the old modal/mode-page expectations.

- [x] 1. New pane follows main rank 1 and level, including real authenticated Codex gpt-6-astra/high. Fixed missing guest.model/effort handoff in d7c27b95; deferred UI, wire regression and real first turn pass (rank1-fixed evidence).
- [x] 2. A per-pane pick leaves the next pane on main rank 1.
- [x] 3. Ctrl+Shift+M → priorities → Alt+Up/Down reorders main; the next pane follows. Live defect fixed in 85d0dd0f.
- [x] 4. `/swap` from a third model goes to rank 1, retains its explanation, then returns.
- [x] 5. Alt+M highlights the current row; arrows skip class headers; text filters; Left/Right collapses/expands a class; Enter selects a flash model; chip is model-only; Escape cancels.
- [x] 6. Alt+E highlights the current level, with all rows inside the window and no scrollbar.
- [x] 7. Board console and terminal offer the same model rows, without role suffixes.
- [x] 8. Guest chips display gpt-6-astra and claude-opus-5, not harness names.
- [x] 9. Models priorities: typing + Ctrl+Enter adds, Delete removes, Ctrl+Z restores. Available folds providers into one model row with via choices.
- [x] 10. No-argument restart preserves each pane's own model, level and role, including flash.
- [ ] 11. PARTIAL: six phone naming tests pass; no paired-phone UI session was available or driven.
- [x] 12. Ctrl+Shift+M opens/closes; Escape returns focus; opening from another terminal re-targets. Ctrl+Alt+M is retired.
- [x] 13. Four tabs: provider dividers/no advanced dialog; alphabetical availability/no recent/no relay-lite, Gemini Flash Lite tickable; sectioned priorities/no lite; jobs reports worker resolutions.
- [x] 14. Unticking an unlisted terminal model removes it from available lookup/Alt+M. Terminal priority entries stay pinned; lite-only membership does not pin; explicit `/model name` may fall back to all usable entries.
- [x] 15. Double-clicking an availability tick does not change the served pane's model.
- [x] 16. Codex catalog fixture offers xhigh/ultra; Relay Free effort is fixed with a reason.
- [x] 17. Models pane and terminal restore together on the saved Models tab.
- [x] 18. No-key first run opens terminal left and expanded providers right.
- [x] 19. Edit an isolated ranking copy, restart worker, fill from defaults: lists follow the edit. Worker restart is required by the file's documented contract.

## Verdict
2026-09-22, Codex: **18 pass, 1 partial/blocked; not a full QA pass.**
The remaining check is live paired-phone presentation. Fixed ranked guest startup in
`d7c27b95ead2fe6c7b332a2a7627c8bac104e0ac`: real signed-in Codex now displays and executes
gpt-6-astra/high on its first prompt; catalog, screenshots, wire regression and logs are in
rank1-fixed evidence. The JS tests do not prove live phone presentation. Found and fixed Alt+Up/Down being intercepted by window pane navigation; only that hunk
landed in `85d0dd0f7dea0023dceaea9b6b8d4d858156805c`, after wrapper and exact-tree builds,
targeted modelpicker/modelspane tests, and a successful live reorder/new-pane regression.

Fresh evidence: `docs/qa_evidence/2026-09-22-verify-MDL1/README.md` (all 19 results, drives,
screenshots, settings, targeted tests). The card stays needs-verification on main. Its existing
uncommitted executing/codex/links front matter belongs to an earlier session and is unchanged;
this verification does not absorb it. Commit and evidence references are recorded here and in
the thread because that links hunk is contested. Pre-existing card-format errors and old thread
ordering are documented in board-check-MDL1.json, not rewritten. CFG1/ACT1/RLP7/XJSN unchanged.
