---
id: V6MP
type: work
status: planned
labels: [feature, models, subagents, ui]
rank: m
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [], related: [4BPE, M9T4], github: null}
---
# Give subagents the main-agent model picker in their pane

## Issue
card needed -- model picker the same for subagents as main agents, and also showing in the subagent pane

## Done means
- A subagent pane's header carries the same model box the main pane has: filled with the same catalog rows as the main picker, opening on that subagent's current model, and reachable with Alt+M while a subagent tab is in front.
- Picking a model there sends `agent_set_model` for that subagent, and the pane follows the `subagent_model` event back: the box label, the tab tooltip and the ✦ status line show the model the subagent will use ("switching (next step)" wording while a run is mid-flight).
- Failure looks like: the box stays empty or hidden in the subagent pane, a pick changes nothing about the subagent's next step, or the subagent box and the main box list different models.

## Plan
**Goal.** Complete the half-built model picker in the subagent pane: the same box, rows and Alt+M the main pane has, aimed at the subagent on the current tab, over the `agent_set_model` message that already exists end to end.

**Findings (all read in the working tree, 2026-09-25).**
- The view half exists and is dead: `SubagentTabsView::m_modelBox` is built in `src/SubagentTranscript.cpp:712-727` (objectName `statusPicker`, accessible "Subagent model"), shown only when a tab is current (`notifyCurrentTab`, :919-927). Its callbacks `onModelPicked` (`src/SubagentTranscript.h:175`) and `onCurrentTab` fire into nothing — no one sets them. The comment at `src/SubagentTranscript.h:174` already names the missing pane function: `Pane::refreshSubagentModelBox`.
- `Pane::adoptSubagentTabs` (declared near `src/Pane.h:1568`; called from `src/RelayWindow.h:3203`) wires every other callback (pause, resume, close, title) but not these two; `Pane::showSubagentTab` calls `tabs->syncRows(m_subagents)`.
- The backend is complete: client message `agent_set_model {id, model}` (`backend/worker.py:733-742`, id may be `"all"`) → `subagents.set_model` (`backend/relay_core/subagents.py:1296-1329`) → emits `subagent_model {id, model, effort, applies: next_step|now}` → `SubagentModel::handle` updates `SubagentRow.model` and adds the "switching" note (`src/SubagentsPanel.cpp:235-249`). The pane feeds worker events at `src/PaneEvents.cpp:58` (`if (m_subagents.handle(event)) return;`), and `SubagentRow` already carries `model`/`effort` (`src/SubagentsPanel.h:11`).
- The main box to mirror: `Pane::refreshPickers` (`src/PaneRuntime.cpp:1406-1490`) fills `m_modelBox` from `modelCatalog()` (`src/Pane.cpp:30`) through `relay::modelrows::fill` with a `modelrows::Context` (`src/ModelRows.h`, `src/ModelPicker.cpp`); picks arrive as data words (`pick:<mode>|<preset>|<model>`, `entry:<preset>|<model>` — `src/ModelRows.h:77`) and are decoded in `Pane::modelBoxPicked` (`src/PaneRuntime.cpp:1302`). Alt+M is the action `agent.modelBox` (`src/Keymap.h:64`) → `Pane::openModelBox` (`src/PaneRuntime.cpp:1318`) → routed from `src/RelayWindowCore.cpp:371`.
- Out of scope: `SubagentsPanel`'s model-chip `m` key and its dead `onPickModel` callback (`src/SubagentsPanel.h:158`, `:646-654`) — that strip widget is constructed by tests only, no product code hosts it. The card is about the subagent *pane*.

**Steps.**
1. Add `Pane::refreshSubagentModelBox(const QString &id)` (declare beside `showSubagentTab` in `src/Pane.h`, define in `src/PaneRuntime.cpp` next to `refreshPickers`): look up `m_subagents.row(id)`; if the pane holds `m_subagentTabs` and the row exists, build a `modelrows::Context` from `modelCatalog()` with the subagent's model as the current row — no mode/role, guest or serving-this-turn rows, a subagent has one model — then `modelrows::fill` into `tabs->modelBox()`, set the collapsed text to the row's model name (`inherit` while unset, matching the tab tooltip's wording in `SubagentTranscript.cpp:188`), and give it the same expand/filter/query callbacks `refreshPickers` gives the main box.
2. In `Pane::adoptSubagentTabs`, set the two dead callbacks: `onCurrentTab` → `refreshSubagentModelBox(id)`; `onModelPicked` → decode the data word to a model key with the *same* helper `modelBoxPicked` uses for its pick/entry paths (factor that decode out, do not copy it), then `send({{"type", "agent_set_model"}, {"id", <current tab id>}, {"model", key}})` in the pane's existing `send` style (`src/Pane.h:1530-1537`).
3. Refresh on change: after `m_subagents.handle(event)` consumes an event at `src/PaneEvents.cpp:58`, refresh the box for the current tab (a `subagent_model` event relabels it); call the same refresh from `Pane::refreshPickers` so preset and catalog edits reach both boxes at once.
4. Alt+M parity: `Pane::openModelBox()` opens `m_subagentTabs->modelBox()` when the subagent tabs view is the pane's current surface and has a current tab, the main `m_modelBox` otherwise.

**Orchestration.** None — one pane-adjacent seam, one session; steps 1-4 touch the same three files in sequence.

**Risks.**
- The worker resolves the sent string with `factory.resolve(model)` (`subagents.py:1306`): a model *name* or preset id. Sending the raw data word, or re-deriving the decode, yields "Unknown model" — hence the shared decode in step 2.
- Mid-run picks apply at the next step (`applies: "next_step"`): the box label must follow the event's `model` and keep the strip's "switching (next step)" wording rather than pretending the switch was instant — `SubagentsPanel.cpp:236-249` has the exact strings.
- `src/Pane.h` is over the file-tool size cap several agents hit; edits there go through `scripts/land.py begin` per repo rules, and `scripts/relay-build` for the build.
- No owner decision is open: the code comment (`SubagentTranscript.cpp:712`, owner, 2026-09-21: "add the model picker in the subagent pane") already asks for exactly this.

**Verify.**
- C++: extend `tests/modelrows_test.cpp` — a subagent `Context` produces the same row list as the main box with the subagent's model as the current row, and the shared decode turns `pick:…`/`entry:…` words into the model key `agent_set_model` needs. Extend `tests/subagents_test.cpp` (its `h.model.handle(...)` harness, :469-472) for the box-relevant `subagent_model` state if the view enters the harness.
- Python: add one test that the worker's `agent_set_model` client message (`backend/worker.py:733`) emits `subagent_model` with `applies` set correctly for idle vs mid-run — the message handler has no direct test today (only the guest bridge path, `tests/test_guest_board_bridge.py:82-95`).
- Manual: run a pane with a live subagent, open its tab, press Alt+M, pick another model; the box label, tab tooltip and ✦ status line change, the strip/tab note says "switching (next step)" while mid-run, and the subagent's next tool call runs on the new model (`rg 'agent=' ~/.local/state/relay/…` or the request ledger).
