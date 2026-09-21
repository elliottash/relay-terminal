---
id: MDL1
type: work
status: needs-verification
labels: [feature, models]
assignee: claude-code
rank: k
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [docs/MODEL-PICKING-DESIGN.md], commits: [bb19b7b0, b2632bf5, 4e14bdca, a61c713b, 9d0b7b8e, 86ddc3b0, 3cac1ecd, f0dede9c, 95773991, 7f8f0616, 0dddb9ee, c5c815de, e76d6eab, e955feb5, 9eccaa8e, 6b2a3c66, a6a7c72e, bae87fd7, 5de41a8f], evidence: [docs/qa_evidence/2026-09-21-model-box-filter, docs/qa_evidence/2026-09-21-model-defaults-and-swap, docs/qa_evidence/2026-09-21-model-dialog-prioritize, docs/qa_evidence/2026-09-21-model-box-modes, docs/qa_evidence/2026-09-21-model-names-everywhere], related: [DC4J], github: null}
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
- [x] Alt+M / Alt+E: current row highlighted, arrows, type to filter <!-- t:a1 -->
- [x] catalog: `models::name`, the worker's `name`, `grouped`, the pane's key resolved by name <!-- t:a2 -->
- [x] defaults: a new pane reads the main list; a pick is per pane; restore the model; re-send tiers <!-- t:a3 -->
- [x] `/swap`: a toggle with memory, a sentence that survives, a test <!-- t:a4 -->
- [x] names at every display site, lower-case roles; no protocol role name ("switchboard") in a box <!-- t:a5 -->
- [x] the box: mode rows "high (model)", Left/Right changes mode in place, that mode's list below, one row per model, `/high` <!-- t:a6 -->
- [x] the Ctrl+Alt+M dialog is where models are picked AND prioritized: tier tabs, reorder, add, remove, level, profile, "via" <!-- t:a7 -->
- [x] the box as classes: top-N per class, Right expands, headers not selectable, exhausted hidden, model alone in the chip <!-- t:a8 -->
- [x] defaults from a ranking file in the repo: 0 providers → Relay Free's three; 1 → one per class; 2+ → two per class, one per provider per class <!-- t:a9 -->
- [x] Options › Models: the tier lists and the checklist leave the page for the dialog <!-- t:a10 -->
- [x] the models pane: providers / available / priorities tabs, opened by Ctrl+Shift+M beside the active pane (again closes; Esc returns focus), first run opens with it at the right; Ctrl+Alt+M and the modal go <!-- t:a11 -->

## Execution Summary
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

## Tests
`ctest --test-dir build -R "modelrows|filterpopup|modelcatalog|modelpicker|panestate|conversations"`
(6/6), and `tests/test_presets.py`, `test_roles.py`, `test_keybindings.py`, `test_model_switch.py`,
`test_conv_index.py`, `test_web_model_name.py`, all run green on 2026-09-21 after the last commit.

## QA checklist
- [ ] A new pane opens on rank 1 of the main list, model and level; a harness at rank 1 says it
      starts on the first prompt, and does.
- [ ] Pick another model in one pane; the next new pane is still rank 1.
- [ ] Reorder main in Ctrl+Alt+M (Alt+Up/Down); the next new pane follows.
- [ ] `/swap` from a third model goes to rank 1, the sentence stays, `/swap` again comes back.
- [ ] Alt+M: current model highlighted; Up/Down; typing filters; Left/Right change mode in place;
      Enter on a flash-list model gives `<model> · flash`; Escape changes nothing.
- [ ] Alt+E: current level highlighted, no scrollbar, inside the window.
- [ ] The Switchboard agent's box is the same list as a pane's, with no "(switchboard)"/"(main)".
- [ ] A Codex pane's box says the model (`gpt-6-astra`), a Claude Code pane `claude-opus-5`.
- [ ] Ctrl+Alt+M: add a model by typing + Ctrl+Enter, Delete it, Ctrl+Z; a model served by two
      providers is one row on `all` with a "via" choice.
- [ ] Quit and reopen with no arguments: each pane is back on its own model and mode.
- [ ] The phone shows the same names.

