---
id: M2C1
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, agent A), 2026-09-18
rank: a
created: '2026-09-18'
acceptance: 'The composer''s model box swaps the pane between the Main and Flash agents in both directions with the chip following, and the ⚙ row opens the model options modal; `docs/qa_evidence/2026-09-18-model-dropdown-selection/` (implementer run under Xvfb); a non-Claude QA session runs the checklist below'
source: 'owner in chat, 2026-09-18 (bug intake): "selecting model options in the model dropdown didnt do anything. the main use case for that is going to be swapping between the main and flash models."'
links: {plans: [], commits: [95f05b4], evidence: ['docs/qa_evidence/2026-09-18-model-dropdown-selection/'], related: [4WHD, C6YX], github: null}
---
# The model box does nothing when you pick anything that is not a provider

## Report

Owner, 2026-09-18: *"selecting model options in the model dropdown didnt do anything. the main use
case for that is going to be swapping between the main and flash models."*

## Root cause

Two separate faults, both in the composer's model box (`Pane::refreshPickers` and the box's
`activated` handler in `src/main.cpp`), and they read as one symptom.

1. **The ⚙ "Model options…" row was dead.** It was added in 585c945 together with a branch of its own
   in the box's `activated` handler; 6f77d4b — the rewrite of the strip into Warp-style chips —
   dropped that branch and kept the row. All that was left was `selectModel`'s guard, which returns
   early on a `gear:` id *precisely because it is not a preset*. So clicking the row did nothing, and
   worse, Qt had already moved the collapsed chip onto "⚙ Model options…", which is not a model.
   README and `docs/ARCHITECTURE.md` had been promising that row opened the Main/Flash/Lite modal.

2. **The Main↔Flash swap was not in the box at all.** The box listed providers only. A pane already
   on the Flash agent had a single `role:` label inserted at index 0 to describe itself, and picking
   that label hit the same early return. There was no Flash row to pick from a Main pane, so the one
   switch the owner wants from the box could only be made with Alt+F, `/flash` or the actions palette.

Not the cause: the signal (`activated` is correct — `currentIndexChanged` would fire on every
rebuild), the `itemData` round trip, or the worker, which has honoured `set_agent_role` all along.

## Change

`src/main.cpp` only.

- The box is now one list of three groups: the stored presets, then a **Main agent** / **Flash agent**
  row per pane role, then the ⚙ gear. Each role row shows the model it resolves to
  ("Main agent · glm-5.3", "Flash agent · glm-5.3-flash"); the live one is ticked, or, when it is not
  Main, is the box's current item — so a Flash pane's collapsed chip reads exactly as it did before
  these rows existed, and the list never shows the same role twice. A role a saved layout restored
  that is neither Main nor Flash gets a row of its own.
- The `activated` handler acts on the two non-preset ids before `selectModel`, which only knows
  presets: `gear:` rebuilds the chip (the gear is not a choice, so the box must not be left sitting on
  it) and opens `openRolesDialog()`; `role:` calls the new `Pane::chooseAgentRole`.
- `chooseAgentRole` calls `setAgentRole` and then rebuilds the chip unconditionally, because
  `setAgentRole` is a no-op when the pane is already on that role and refuses mid-turn — and in both
  cases Qt has already moved the box to the row that was clicked. The role id goes through
  `canonicalRole`, so a restored `fast` row lands on `flash`.
- `Pane::roleModelText` is the text beside a role: the pane's live model when the pane is running that
  role, otherwise the model the worker resolved the role to, and nothing when no key has resolved yet.
- Shortcut hint (WARP.md standing rule): picking a role row with the mouse shows `model.role.mouse`,
  "Next time: Alt+F · the Main and Flash agents", built from the live Keymap text. The gear row shows
  `model.options.mouse`, which points at Actions › Settings › Models (the gear has no key of its own).
- Docs: the model-box contract is written down in `docs/ARCHITECTURE.md` ("Model roles and the
  Main / Flash / Lite tiers") and `README.md` now lists the row beside `/flash` and Alt+F.

Selection takes effect on the pane's next turn through the existing path: `set_agent_role` →
the worker's `agent.set_model` between turns → `model_changed` back with `agent_role`, which sets
`m_model`/`m_agentRole` and rebuilds the chip. The conversation is kept, and no model call is made to
do it. Picking a *preset* still puts the pane back on the Main agent, as before (protocol 13).

## Implementer evidence

Build clean, `ctest --test-dir build` 25/25 and `./scripts/test.sh` 870 tests OK on a tree carrying
this change alone (2026-09-18). Re-checked on main after the code landed in 95f05b4, with the rest of
the day's work beside it: **`ctest` 29/29 and `./scripts/test.sh` 883 OK**. An intermediate run had
failed `test_remote_browser.test_pair_and_drive_a_pane_from_the_browser` (the web client waiting for
`.plan-title`); that was the concurrent remote-share rewrite of the web client, which has since
landed, and the group passes again. Nothing in that path — `remote/`, `src/RemoteShare.*`, the web
client — is touched here.

Live under Xvfb with an isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`,
`RELAY_KEYRING=off` and a literal non-key string in `RELAY_GLM_CODING_API_KEY` (no account, no turn,
nothing sent to a provider) — `docs/qa_evidence/2026-09-18-model-dropdown-selection/drive.sh`:

| File | What it shows |
|---|---|
| `implementer-a-list-open.png` | the open list: `glm-5.3`, `✓ Main agent · glm-5.3`, `Flash agent · glm-5.3-flash`, `⚙ Model options…` |
| `implementer-b-flash.png` | the Flash row picked: the chip reads "Flash agent · glm-5.3-flash" |
| `implementer-c-main.png` | the Main row picked: the chip is back to "glm-5.3" |
| `implementer-d-flash-again.png` | Flash a second time — byte-identical to b, so the swap is repeatable both ways |
| `implementer-e-still-flash.png` | the same pane, still open, after typing in the composer: still Flash |
| `implementer-f-model-options.png` | the ⚙ row opened the Main / Flash / Lite modal, and the chip stayed on the pane's agent instead of sitting on the gear |
| `implementer-g-no-stored-keys.png` | with no stored key at all: "No stored keys", the two role rows with no model yet, and the gear still reachable |

There is no unit-test seam: `src/main.cpp` is the `relay` executable and no test target compiles it,
so the drive script is the regression check. See "Known gaps".

## QA checklist

1. **The list.** With at least one stored key, open the composer's model box. Under the presets there
   is a separator, a **Main agent** row and a **Flash agent** row, each with the model it resolves to,
   then a separator and **⚙ Model options…**. On a Main pane the Main row carries a tick and the
   collapsed chip is the preset.
2. **Main → Flash.** Pick the Flash row. The chip becomes "Flash agent · <flash model>", a toast says
   "Flash agent for this pane · <model>", and the next prompt is answered by the Flash model with the
   conversation intact.
3. **Flash → Main.** Pick the Main row. The chip goes back to the preset's model and the conversation
   is still intact. Repeat 2 and 3 a few times in the one pane, with turns in between.
4. **The pane stays open.** After switching to Flash, run a shell command, open and close a file pane,
   split and un-split. The chip still says Flash and a prompt still goes to the Flash model.
5. **The gear.** Pick **⚙ Model options…**: the Main / Flash / Lite modal opens and the chip is
   unchanged — it must never be left reading "Model options…". Close the modal, pick the gear again.
6. **Presets still work.** With the pane on Flash, pick a provider row: the pane switches provider
   *and* returns to the Main agent. With the pane on Main, pick a different provider: it switches.
7. **Busy pane.** With a turn running, pick the Flash row: the status line says "Stop the current agent
   turn before switching the pane's agent." and the chip snaps back to what the pane is actually on —
   it must not be left showing a role the pane is not running.
8. **No stored key.** With no key at all the box reads "No stored keys", and the role rows and the gear
   are still there and still work (the gear is the way to fix it).
9. **Hints.** First time the Flash row is picked with the mouse, "Next time: Alt+F · the Main and Flash
   agents" appears; it respects the per-hint limit and Settings › General › Shortcut hints. Rebind
   `agent.flashAgent` and confirm the hint quotes the new key.
10. **Vision turn.** Send a prompt carrying an image on a pane with a vision model: the chip shows
    "🖼 <model> · this turn" while the turn runs and goes back to the pane's own row afterwards; the
    role rows are still listed underneath.
11. **Restored layout.** A saved layout whose pane says `agent_role: "fast"` comes back with the Flash
    row selected (`canonicalRole`), not an extra third row.

## Known gaps

- No automated regression test. `src/main.cpp` is a single translation unit built only into the `relay`
  executable, so nothing in `tests/` can construct the model box; adding that seam means lifting the
  composer's chip strip out of `main.cpp`, which is worth doing but is not this fix. `drive.sh` in the
  evidence folder is the repeatable check in the meantime.
- ~~The intake line is still in `issues/bug_intake.txt`.~~ Filed: the three 2026-09-18 bug-intake
  lines this card came from were cleared from the intake when the day's work was swept up. This card
  is the record.
- The subagent rows' own model menu (`Pane::pickSubagentModel`) was checked and is a separate, working
  path — it is a `QMenu`, not this box, and it has no role rows. Whether it should offer Main/Flash per
  subagent is a product question, left alone.
