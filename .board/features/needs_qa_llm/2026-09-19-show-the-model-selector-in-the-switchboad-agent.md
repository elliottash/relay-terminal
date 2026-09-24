---
id: BRD3
type: work
status: needs-qa-llm
labels: [feature, switchboard]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: ['0a7a43d7'], evidence: [docs/qa_evidence/2026-09-20-switchboard-model-box/], related: [], github: null}
---
# show the model selector in the switchboad agent

## Issue
show the model selector in the switchboad agent (so i can pick a different model)

## Plan
**Goal.** A model selector in the Switchboard pane that shows the model the Switchboard agent is running and lets the owner pick a different one.

**Findings (code today).**

- One `BoardWorker` per workspace (`src/BoardWorker.h/.cpp`, one `backend/worker.py`) runs the Switchboard agent. `RelayWindow::startBoardWorker` (`src/RelayWindow.h:4693`) builds its `configure` from QSettings — `provider/*`, `Pane::rolesObject()`, `agent_role: "switchboard"`, the `board` block — and `reconfigureBoardWorkers()` (`src/RelayWindow.h:4731`) re-sends it to every live worker when provider/roles settings change.
- The worker resolves the `switchboard` role (`backend/relay_core/roles.py`; `ROLE_TIERS["switchboard"] = "main"`, so it follows the Main tier by default) and builds the agent from it **at configure time** (`backend/worker.py`, `configure` branch). A later `set_agent_options` role change does not move the running agent; only a fresh `configure` does, and configure refuses while a turn runs.
- The `configured` event already carries `model`, `agent_role` and full `roles`/`tiers` summaries (`backend/worker.py:210`), and the worker answers `presets` with every provider row. All BoardWorker events reach `BoardView::handleEvent` (`src/RelayWindow.h:4633`), which ignores `configured`/`presets`/`model_roles` today — `src/BoardPane.cpp` shows no model anywhere. The only way to change it is the roles modal's Advanced row "Switchboard card threads" (`RolesDialog`, `src/ModelSettings.h`; QSettings keys `roles/switchboard/{tier,preset,model,effort}`, written at `src/ModelSettings.cpp:922-969`).
- The style to mirror is a pane's model box: `Pane::m_modelBox` (`src/Pane.h:3563`), built by `updateModelBox` (`src/Pane.h:10326`), chosen in `modelBoxPicked` (`src/Pane.h:10189`).

**Design.** The selector edits the persisted `switchboard` role and reconfigures the workers — the same setting the roles modal edits — not a one-off `set_model` to the worker, which `reconfigureBoardWorkers()` would silently overwrite and which would relabel the worker's role.

**Steps.**

1. `BoardView` (`src/BoardPane.h/.cpp`): add a compact `CurrentTextComboBox` (objectName `statusPicker`, accessible name "Switchboard agent model", `QSizePolicy::Maximum` as in `src/Pane.h:3563-3571`) to the tools row beside `m_sort` (`src/BoardPane.cpp:2470-2516`). Rows: "Follow Main — <model>" (the default), Flash and Lite tier rows with their live models, then one row per usable provider (stored key, Relay Free, local endpoints — **not** guest harnesses, whose ids `roles.validate_roles` would reject), then "⚙ Model roles…".
2. Data in: send `{"type":"presets"}` through `onSend` once configured (widen its BoardPane.h comment from "a board_* protocol message" to a worker protocol message) and cache the rows; handle `configured`, `presets` and `model_roles` in `handleEvent` (`src/BoardPane.cpp:2904`) to set the current row from `roles.switchboard` and carry its `warning`/`note` in the tooltip. Re-request `presets` when keys change (the window's existing `reconfigureBoardWorkers` hook).
3. Pick: a new `std::function` callback on BoardView (keep QSettings writes out of the view): tier pick writes `roles/switchboard/tier` (empty for Main), provider pick writes `roles/switchboard/preset` and clears `tier`/`model` — the same keys `RolesDialog` writes; factor that small write into a shared helper in `src/ModelSettings.h` so the two writers cannot drift. Then `reconfigureBoardWorkers()` so every open board's worker rebuilds on the new role. The gear row runs the existing `agent.modelRoles` action (`src/RelayWindow.h:1167`).
4. Disable the box while any Discuss/Plan/cleanup turn runs (busy is already tracked, `src/BoardPane.cpp:1413`): the worker refuses a configure mid-turn, so the pick must wait rather than error.
5. Protocol doc: `docs/AGENT-SESSIONS-PROTOCOL.md` §19 — note that the Switchboard pane sends `presets` and reads `configured.model`/`roles`/`model_roles` for its selector. No new messages or events.
6. Tests in `tests/boardmodel_test.cpp` (BoardView driven by hand, as the file already does): rows built from a fed `configured`+`presets`; a pick emits the expected writes and reconfigure; busy disables the box; a keyless provider falls back to Main with the warning in the tooltip. `tests/test_roles.py` already covers resolution; no backend change is expected.

**Risks.**

- A pick reconfigures the worker, which replaces its agent and conversation — that is the existing semantics of a provider/roles change, and mid-turn picks are blocked by step 4, but nothing else is interrupted silently.
- One `roles/switchboard` setting is shared by every open board (all workspaces): a pick in one Switchboard pane moves them all. That matches the roles modal; **question for the owner — global is recommended, but say so if you want the model picked per board/workspace** (that would need a new per-board setting and a configure override).
- Width: the tools row lives in a pane that can sit at a third of the window (`src/Pane.h:3475`); keep the box collapsed to its current text, as the pane's model box does.

**Verify.**

- `./scripts/test.sh` and `ctest --test-dir build`, including the new `boardmodel_test.cpp` cases.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: open the Switchboard (Ctrl+Shift+S) — the box shows the live model; pick Flash — the box shows the Flash model and a Discuss turn answers on it (worker's `configured` event, `agent_role: switchboard`); pick a provider with no stored key — the box falls back to Main with the warning tooltip; the box is disabled while a Plan turn runs; the roles modal's "Switchboard card threads" row agrees with the pick in both directions.

## QA checklist

- [ ] Open the Switchboard (Ctrl+Shift+S): the page agent's composer row ends with the model
      box naming the live model — "Follow Main — <model>" by default — left of the microphone.
- [ ] Open the box: "Follow Main — <model>", then Flash and Lite with their live models, then
      one row per usable provider (stored key, Relay Free, local servers — never a guest row),
      then "⚙ Model roles…" last. Collapsed, it is only as wide as the model it names.
- [ ] Pick Flash: the box shows the Flash model and a Discuss turn answers on it (the worker's
      `configured` event, `agent_role: switchboard`). A freshly opened Switchboard agrees.
- [ ] Pick a provider with no stored key: the box falls back to Main with the warning in its
      tooltip.
- [ ] While a Discuss, Plan or cleanup turn runs the box is disabled; it comes back after.
- [ ] The gear row opens Model roles; its Advanced row "Switchboard card threads" agrees with
      the box in both directions.
- [ ] The pick is global across every open board (matches the roles modal): a pick in one
      Switchboard moves them all.
