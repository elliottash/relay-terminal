---
id: FEJQ
type: work
status: discussing
priority: 1
rank: zzzzzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# allow agents to control the relay app, and add helper agents in the options, ses…

## Issue
allow agents to control the relay app, and add helper agents in the options, sessions, and actions menus.

give the agents skills to change options, run actions, or search the sessions manager, like they currently have for the switchboard.  

it can also open those panes if you ask it, and zoom to specific items / options.. if they cant yet, also allow agents to open the switchbaord and cards directly as well and zoom to them. 

when you are in options, actions, or sessions, you have a helper agent, same as the switchboard agent.

## Plan
**Goal.** Agents can drive the Relay app itself: read and change options, run actions, search the session manager, and open/zoom the Options, Actions, Sessions and Switchboard panes (down to a row, a search query, or a card). The Options, Actions and Sessions panes each get an embedded helper agent, like the Switchboard agent on cards.

**Findings.**
- The Switchboard agent is the model to copy: a per-window lightweight worker (`src/BoardWorker.{h,cpp}`, started with `agent_role: "switchboard"`, RelayWindow.h:4604 `startBoardWorker`), backend tool set in `backend/relay_core/board_tools.py` (`BoardTools`), attached to the agent as `agent.board` and merged into the tool list in `backend/relay_core/agent.py` (`Agent.tools()`, :637-645; dispatch :2194-2228). Pane-embedded chat UI lives in `src/BoardPane.{h,cpp}` (card thread + reply box).
- Options and Actions are one widget, `src/SettingsPane.{h,cpp}` (`Mode::Options`/`Mode::Actions`), fed by two GUI-side catalogs RelayWindow builds: `settingsSections()` (`SettingRow` with stable `id`, reader/writer closures, kinds Toggle/Choice/Text/Number/Button…) and `searchableActions()` (`ActionItem` with `key`, `run`). Navigation entry points already exist: `openSettingsPane(mode, tab, search)`, `SettingsPane::revealOption(sectionId, rowId)` (the "zoom to an option" primitive), `openSessions(tab, query)`, `openBoardCard(id)` (RelayWindow.h:1255, 3740, 4427).
- The Sessions pane (`src/Conversations.{h,cpp}`, `SessionManager`) never talks to the worker itself; search is worker-side via the protocol-14 `conversations` message (`backend/relay_core/session_protocol.py`, `SessionIndex.search`), so a backend tool can search sessions without the GUI.
- Precedent for shipping a GUI catalog to the worker: the `keybindings` catalog — sent in `configure`, refreshed by a `keybindings` message (`backend/worker.py`:148, 239-246). Options/actions catalogs can ride the same pattern.
- There is **no** worker→GUI navigation/command channel today; Pane handles a fixed set of event types (Pane.h:8891-9352). A new event type is needed, forwarded Pane → RelayWindow the way `onOpenCard` is (Pane.h:648, RelayWindow.h:4944).
- Roles are fixed in `backend/relay_core/roles.py` (`ROLES`, `LABELS`, defaults :50-110); a helper role must be added there and in protocol 13.1.

**Steps.**
1. **Protocol (docs/AGENT-SESSIONS-PROTOCOL.md).** New section 30 "The agent drives the app": the `app` block on `configure` (options catalog: section/row ids, labels, kinds, current values, `settable` flag; actions catalog: key, section, label, `agent_safe` flag), an `app_catalog` refresh message (mirrors `keybindings`), the `app_command {command, …}` worker→GUI event and its `app_command_result {id, ok, error}` GUI→worker answer, and the new `helper` role in 13.1. Update docs/ARCHITECTURE.md.
2. **Backend `backend/relay_core/app_tools.py` — `AppTools`, mirroring `BoardTools`.** Tools: `app_option_list`/`app_option_get {section?, row?}`; `app_option_set {section, row, value}` (validated against the catalog; secrets rows — API keys — are never settable); `app_action_list {search?}`, `app_action_run {key}` (only `agent_safe` actions); `app_sessions_search {query, scope?, limit?}` answered worker-side through the session index; `app_open {target: options|actions|sessions|switchboard, section?, row?, query?, card?}` emitting `app_command {command: "open"}`. Writes/action runs emit `app_command` with a request id and block on `app_command_result` (the `BoardInit.ask` round-trip pattern), so the tool result says what actually happened. Attach as `agent.app` in `agent.py` exactly like `agent.board`, with a short system-prompt section.
3. **Worker wiring (`backend/worker.py`).** Parse the `app` block of `configure` and the `app_catalog` refresh into a catalog object (like `KeybindingCatalog.from_request`); build `AppTools` and hand it to the `Agent`; route `app_command_result` to the pending tool call.
4. **GUI command handling.** RelayWindow builds the app catalog from `settingsSections()` + `searchableActions()` and sends it with every pane `configure` (and on `SettingsWatch` change). Pane gains `onAppCommand`; RelayWindow executes: `open` → `openSettingsPane` / `revealOption(section, row)` / `openSessions(tab, query)` / `openBoardCard(card)`; `set_option` → find the row and invoke its writer; `run_action` → find the `ActionItem` and `run()` it. Every outcome answers `app_command_result`. ActionItems and SettingRows gain an `agentSafe`/settable marker; destructive actions (delete session, reset to defaults) and secret rows default to off.
5. **Helper agents in the three panes.** One per-window helper worker (reuse/generalize `BoardWorker`, or a sibling `HelperWorker` on the same NDJSON pattern) configured with `agent_role: "helper"`, the app catalog and `AppTools` plus the card-read tools. A compact chat surface (input box + answer area, modelled on the card thread reply box in `BoardPane`) embedded in `SettingsPane` (visible in both modes) and in `SessionManager`. Asks are ordinary turn events routed to the asking pane; each pane's ask carries a `pane` hint ("options" / "actions" / "sessions") that picks the brief, so the helper answers about the pane it lives in.
6. **Roles (`backend/relay_core/roles.py`).** Add `"helper"` to `ROLES`, `LABELS`, the defaults table (defaults to main, like `switchboard`) and the GUI role labels (Pane.h:1026-1042).
7. **Shortcut hints (WARP.md standing rule).** If the helper chat or agent-open flows get keys/slash entries, register hints in the shortcut-hint registry.

**Risks / decisions for the owner.**
- **Which options an agent may set.** Recommend: Toggle/Choice/Number/Text rows marked settable, never API-key/secret rows, never Button rows. Needs the owner's call on the default set.
- **Which actions are `agent_safe`.** Recommend opt-in per action; delete/reset/pairing actions stay off until the owner says otherwise.
- **One helper worker per window** (recommended, brief chosen by pane) vs one per pane type — a worker per pane triples the model warm-ups.
- Whether the main pane agent gets `app_option_set`/`app_action_run` by default or behind an Options › Agent toggle (recommend: on, with a toggle).

**Verify.**
- Backend: new `tests/test_app_tools.py` (catalog validation, set/open/run emit the right `app_command` and honour the result round-trip, sessions search against a stub index, secrets refused) and protocol tests for the `configure` block and `app_catalog` refresh, run as `pytest tests/test_app_tools.py` plus the touched protocol test subsets.
- GUI: `ctest --test-dir build -R` for the touched suites (settingspane/conversations/boardmodel tests as extended); catalog build and `app_command` routing covered headless.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: from a terminal pane, ask the agent to open Options at a specific row and flip a toggle, run a safe action, search sessions, and open `#FEJQ` on the Switchboard; then ask the helper inside each of Options, Actions and Sessions a question. Screenshots and the driver script under `docs/qa_evidence/2026-09-…-agent-app-control/`.
