---
id: 6VMF
type: work
status: planned
labels: [feature, gui, worker, onboarding]
component: [gui, worker]
milestone: beta
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [probe, person], human: required, criteria: 'on a fresh XDG_CONFIG_HOME the window opens with the terminal left and the Start pane right, no dialog appears, each recipe''s Enter produces a first agent turn without an error, Esc leaves only the approvals pick and the pane never returns after it', sign_off: none, effort: high, stakes: reputation, blast: capability}
source: 'Claude Fable session in Relay, 2026-09-24, delivery card 1 of 3 from #9HS0'
links: {plans: [], commits: [], evidence: [], related: [9HS0, K2FV, ZYRB, MH58, HG7K, SZ1H], github: null}
---
# Start pane and starter-task recipes: one first-run screen replaces the instructions and approvals dialogs

## Issue
all recs approved, go ahead and file the three delivery cards linked to #9HS0.

## Done means
- A fresh profile (empty `XDG_CONFIG_HOME`) opens one terminal pane at the left and the **Start** pane at the right. No instructions dialog at 400 ms, no approvals pane at 800 ms, no Models pane. The terminal is live before the Start pane is read.
- The Start pane holds, top to bottom: a health line only when the Python backend or a shell is missing; one orientation sentence; the eleven starter tasks; a keys row (Relay preselected; Warp, VS Code, Konsole); the #K2FV approvals row with its three sentences and two buttons, unchanged; the footer *Done this before? Esc closes this and it will not come back. /start reopens it.*
- Enter on a task stages its recipe: the folder row when the task needs a folder ("Choose a folder…" or "Make one for me", which creates `~/Relay/<task>/`), the layout, the first prompt in the composer under the agent prefix and **not sent**, and a one-line "needs …" with the fix when a requirement is missing. If the approvals row is unanswered, it is asked first.
- Esc collapses the tasks and leaves only the approvals row with "Allow everything — recommended" focused; the pane closes on the pick. Closing the pane by any route sets `instructions/onboarded`; it never reopens by itself. `/start`, Options › General › "Show the Start pane" and the Actions palette reopen it.
- "Import my settings" shows what it found from Claude Code, Codex and Warp ("3 instruction files · 2 keys"), every box unchecked, and is hidden when nothing is found; the starter `relay.md` is still written silently when nothing is imported (#ZYRB).
- A recipe files a Board card only when the folder already has a Board.
- Failure looks like: any dialog before the Start pane; a recipe whose first Enter errors on a fresh machine; the Start pane reappearing after Esc; the Models pane greeting a fresh profile; `instructions/onboarded` unset after the pane closed.

## Plan
### Goal
One first-run screen, the Start pane, replaces the instructions dialog and the approvals pane and is where a newcomer picks a first task and an expert presses Esc. Design and decisions: #9HS0 and `reports/Beginner UX and onboarding for Relay.md` §3.2, §3.3, §3.8; decisions 1–19 on #9HS0's thread are settled and are not reopened here.

### Findings
- First-run layout: `WindowManager::newWindowAt` (`src/WindowManagerImpl.h:301-318`) opens terminal + Models pane on the providers tab when `instructions/onboarded` is unset.
- The two timers: `Pane::onSessionConfigured` (`src/Pane.h:5798-5812`) fires `openInstructions()` at 400 ms and `onApprovalsChoice` at 800 ms; the approvals screen is `ApprovalsView` (`src/ApprovalsPane.h:48-103`), wired by `RelayWindow::openApprovalsPane`, writing `security/approvals_chosen`.
- Instructions dialog: `relay::agentui::chooseInstructions` (`src/AgentUi.h:49`, `src/AgentUi.cpp:132-199`); the silent starter `relay.md` path is `Pane::initDefaultRelayMd` (`src/Pane.h:8305-8340`).
- Key import from Warp / Claude Code / Codex: Actions › API keys… in `src/RelayWindowModels.cpp` (`askForKey`, the import actions).
- Keybinding presets: `docs/KEYBINDING-PRESETS.md`, `Keymap` (`src/Keymap.h`), the Options › General preset row.
- Folder dialog: `QFileDialog::getExistingDirectory` (`src/RelayWindowCore.cpp:609`); nothing creates a project folder today.
- Composer prefill: the `app_prefill_prompt` path (`backend/relay_core/tools.py`, `src/Pane.h` prefill handler) already puts text in the box without sending; the `*` prefix chip (`Pane::setPrefixMode`, `src/Pane.h:10213`) forces agent routing.
- Task plugins and their `requires` check: `backend/relay_core/task_plugins.py`, `docs/TASK-PLUGINS.md`. Board init question: `docs/PROJECT-INIT-AND-IMPORT.md`, `board_init_request`.
- Slash commands: the registry `updateSlashPopup` reads (`src/Pane.h:13097`); `/instructions` is the model for `/start`.
- Health: `packaging/installed-smoke.py` checks the bundled backend in CI; the pane already knows when the worker failed to start (`configured` never arrives).

### Steps
1. **Recipes as data.** `backend/relay_core/start_recipes.py` loads `backend/relay_core/recipes_bundled/<id>.json` (id, title, one-line blurb, `needs_folder`, `layout`, `prompt` with `{folder}`/`{host}` slots, `requires` in the task-plugin vocabulary, `plugin`, `skill`, `arms` hint ids, per-platform overrides for paths and programs) and answers a `start_recipes` protocol request with each recipe's status on this machine: `ready`, or `needs` with the one-line fix. An installed skill or task plugin may declare a recipe of its own. Eleven bundled recipes per §3.3 ("Triage my email" and "Organize multiple subscriptions" say `needs a skill` until #SZ1H). Protocol section added to `docs/AGENT-SESSIONS-PROTOCOL.md`. Tests: `tests/test_start_recipes.py`.
2. **The pane.** `src/StartPane.{h,cpp}`, a `PaneView` in the shape of `ApprovalsView`: health line (only when the worker did not configure), orientation sentence, the recipe list (arrow keys, Enter, type-to-filter), the keys row (writes the preset the way Options › General does), the approvals row embedding `ApprovalsView`'s three sentences and two buttons and its two callbacks unchanged, the footer. Typography and the two destination colours only (#8E4Q). Add to the `relay` target in `CMakeLists.txt`.
3. **Behaviour.** Enter on a recipe: if `security/approvals_chosen` is unset, focus the approvals row with "one thing first" and return; else folder row when `needs_folder` ("Choose a folder…" = the existing dialog; "Make one for me" = `mkpath(~/Relay/<title>)`), then apply the layout, cd the pane, stage the prompt under the `*` prefix, file a card only when `<folder>/.board/` exists. Esc: collapse to the approvals row, focus "Allow everything — recommended"; the pick closes the pane. Any close sets `instructions/onboarded`. `/start`, Options › General › "Show the Start pane", and an Actions row reopen it.
4. **Retire the greeters.** `newWindowAt` opens terminal + Start pane on a fresh profile; the 400 ms and 800 ms timers in `onSessionConfigured` are removed (the `/instructions` command and Options › Security › "show the first-run choice again" keep their dialogs). `initDefaultRelayMd` still runs when the pane closes with nothing imported.
5. **Import row.** Reuse `chooseInstructions`' file scan and the key importers to render "Import my settings · 3 instruction files · 2 keys", boxes unchecked; hidden when both scans are empty.
6. **Docs and README.** `docs/ARCHITECTURE.md` (first run), README Quick start step 1 rewritten around the Start pane, `docs/SWITCHBOARD-AESTHETIC.md` §2.1 row updated to "built".
7. **Evidence.** Xvfb drive on a fresh `XDG_CONFIG_HOME` into `docs/qa_evidence/<date>-start-pane/`: a screenshot of the pane, one per recipe after Enter, the Esc path, a relaunch showing the bare pane; `packaging/installed-smoke.py` extended to assert no dialog appears.

### Risks
- `instructions/onboarded` is read in two places with two meanings (layout gate, welcome offered); step 4 keeps it as the single gate and documents it.
- The approvals row must keep #K2FV's guarantee that the cautious set holds until the pick; embedding `ApprovalsView` rather than copying its text keeps one source.
- "Make one for me" creates a folder under `~`; it must never create a Board there uninvited (`docs/PROJECT-INIT-AND-IMPORT.md`).
- macOS and Windows: recipes are platform-aware in data; the first delivery is verified on Linux, the other two as follow-ups on this card.

### Verify
- `PYTHONPATH=backend python3 -m unittest tests.test_start_recipes -v`
- `ctest --test-dir build -R startpane` (a new case beside the pane tests: layout on a fresh profile, Esc path, `onboarded` written)
- The Xvfb drive above; a person looks at the screenshots (verify block: human required).
