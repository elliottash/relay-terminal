---
id: 64KE
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: tm
created: '2026-09-17'
acceptance: quitting Relay with several windows, tabs and split panes and reopening it brings back the same layout, each pane in its old directory with its agent conversation reattached; a fresh profile still opens a single pane
source: 'owner in chat, 2026-09-17: "i want it to save and persist, and when you re-open, its back to where you were, like in warp"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Restore windows, tabs, panes and their conversations on start

## Today

Agent conversations already persist per pane (`~/.local/share/relay/sessions/<workspace hash>/`, saved after
every turn; `/resume` reopens one). Nothing else does: windows, tabs, the pane tree, each pane's directory,
engine, model/effort/mode and its session id are lost on quit, and a restart opens one fresh pane.

## Scope

1. **Layout state file** per user (e.g. `~/.local/share/relay/state/windows.json`, 0600), holding for each window:
   geometry and screen, its tabs (title, order, current), and each tab's pane tree (split direction and sizes),
   with per pane: workspace, cwd, engine (`konsole`/`relay`), provider preset/model/effort, agent mode,
   input mode, session id, and tool panes (explorer/preview/plan/transcript) with their paths.
2. **When to write**: debounced on change (split, close, move, tab change, cwd change, model change, window move)
   and on quit; atomic write; a crash must never lose more than the debounce window.
3. **Restore on start**: rebuild windows, tabs and panes; each pane starts its shell in its old cwd and asks the
   worker to load its session id (`resume`), printing the usual short recap line and open-task count. A pane whose
   session file is gone starts fresh with a note. Terminal scrollback is not restored (the shell is new).
4. **Controls**: Agent options / settings toggle "Reopen windows on start" (default on, like Warp), a
   `--fresh` flag and a palette action "Start a fresh window set" that ignores the saved state.
5. **Edge cases**: multiple Relay instances (last writer wins, or per-instance state keyed by a start token —
   decide and document), a workspace folder that no longer exists, screens that disappeared (clamp geometry),
   Wayland (no geometry restore), and the existing "restore last closed pane/tab/window" (Ctrl+Shift+W) staying
   independent.

## Open questions — answered by the owner, 2026-09-17

1. Restore **silently** on start, no prompt ("like in warp").
2. A restored pane **re-runs nothing**.
3. **One** saved layout; no history of layouts.

## Implemented

### The file

`$XDG_DATA_HOME/relay/state/windows.json`, mode 0600, written atomically (`QSaveFile`: temp file +
rename), with a schema version:

```json
{"version": 1, "saved": 1789670721,
 "windows": [{"geometry": [40, 30, 1100, 800], "screen": "DP-1", "current": 0,
              "titles": ["alpha  ·  2", "gamma"],
              "tabs": [{"split": "h", "sizes": [547, 547], "children": [
                          {"pane": {"cwd": "…/alpha", "workspace": "…/alpha", "engine": "relay",
                                    "agent_role": "main", "agent_mode": "build", "input_mode": "auto",
                                    "effort": "high", "preset": "kimi", "model": "kimi-k3",
                                    "session_id": "ee49…"}},
                          {"pane": {…}}]},
                       {"pane": {…}}]}]}
```

- The pane/split/tool nodes are **exactly** the ones `RelayWindow::serializeNode()` already produced
  for "restore last closed pane/tab/window", extended in place with `agent_mode`, `input_mode`,
  `effort`, `preset`, `model` and `session_id`. There is one layout format in the app, not two, and
  Ctrl+Shift+W benefits from the extra fields as well. `engine_core` is only written when the pane
  has an explicit core, so `--engine-core` still decides on the next start.
- `titles` is informational — Relay derives tab titles from the panes, so a restore recomputes them.
  KDE's `KAcceleratorManager` puts `&` accelerators into tab texts; they are stripped before saving.
- Tool panes (explorer / preview / plan) are saved and restored with their paths, as before. Live
  subagent and turn transcripts are not saved (they never were: `ToolPane::node()` is empty for them),
  and a tab that holds nothing else is dropped from the saved set.

### New unit: `src/WindowState.{h,cpp}` (library `relay-windowstate`, plain QtCore)

Everything that does not need a window, so it can be tested on its own: default paths, the document
wrapper, `write()`/`read()` (atomic, 0600, version-checked), `isUsableNode()`/`usableWindows()`
(drops unknown node kinds, empty splits and trees nested deeper than `kMaxDepth` = 24, and clamps the
current-tab index), `clampToScreens()` and `resolveDirectory()`. `tests/windowstate_test.cpp`
(ctest `windowstate`, 22 cases) covers all of it, including both engines round-tripping.

### When it writes

`WindowManager::scheduleSave()` is a 1 s single-shot debounce, so a crash loses at most that.
It is called from `RelayWindow::updateTitles()` (which already runs after every split, close, tab
change, directory change and model/role/mode change), `QSplitter::splitterMoved`, `moveEvent`,
`resizeEvent` and `closeEvent`; `QCoreApplication::aboutToQuit` flushes.

**Quit versus closing a window.** `closeEvent` snapshots the whole window set *before* the window
leaves it and settles on the next event-loop turn: if no window is left (a quit), the whole snapshot
is written, so everything that was open comes back; if other windows survive, the remaining set is
written, so a window you deliberately closed stays closed. An empty set is never written — quitting
is not a request to forget the layout.

### Restore on start

Silent, no prompt. Skipped when `--fresh` or an explicit `--workspace` is given (both mean "open a
new window"), when the setting is off, when another Relay owns the file, or when the file is
missing/unreadable (one line on stderr and in the status bar, then a normal single window).

Each restored pane starts a **new shell** in its old directory and, once its agent is configured,
sends `resume {session_id}`; the worker answers `state_loaded` and Relay prints the usual
`Session loaded: "<title>" · N turn(s)` line and the `○ N task(s) still open · /tasks` line, followed
by the normal post-resume recap. Nothing is re-run and terminal scrollback is not restored.
A session whose file is gone prints one line — `Previous conversation is no longer saved · starting a
new one` — and a `resume` the worker refuses prints
`Previous conversation could not be reopened (<reason>) · starting a new one`.

### Controls

- **Setting** `windows/restore`, default on: palette → **"Reopen windows on start"** (in the
  windows/panes section, next to "Restore closed"). Turning it off deletes the saved file.
- **`relay --fresh`**: ignores the saved layout once and keeps the file.
- **Palette action `windows.fresh`, "Start a fresh window set"**: deletes the file and stops saving
  for the rest of the session, so the next start opens one new window. It is in the keymap (unbound
  by default, so a user can bind it) and, per the standing shortcut-hint rule, running it from the
  palette shows a hint — the bound key if there is one, otherwise
  "Next time: start Relay with --fresh to skip the saved layout once" (hint id
  `windows.fresh.palette`).

### Edge cases

- **A second Relay instance — the decision.** The file has **one owner at a time**, a `QLockFile` at
  `$XDG_RUNTIME_DIR/relay/windows.lock` taken at start and held for the life of the process. Only the
  owner restores on start and only the owner saves, so a second Relay opens a plain window, says so in
  the status bar ("Another Relay is running; this window set is not saved.") and leaves the layout
  alone. If the owner quits, the next save by a still-running Relay takes the lock over and that
  instance's windows become the saved set from then on. Where there is no runtime directory (so no
  lock) this degrades to **last writer wins** — still atomic, so the file always holds one complete
  layout and can never be corrupted by a concurrent writer.
- **A cwd or workspace that no longer exists**: `resolveDirectory()` falls back to the pane's
  workspace, then to the window's workspace, then to `$HOME`. This now also applies to
  Ctrl+Shift+W restores and `relay open`.
- **A screen that vanished**: `clampToScreens()` reuses the saved screen when its name is still
  there; otherwise the screen holding the window's centre, then one it overlaps, then the first one.
  The window is shrunk to fit and moved fully inside the available area.
- **Wayland**: clients cannot place their own windows, so only the size is applied and the compositor
  places the window; the saved position is kept in the file and used again under X11.
- **Ctrl+Shift+W stays independent**: `closed.restore` is the in-session stack of up to 25 closed
  items and is not affected by the setting, `--fresh` or "Start a fresh window set".
- **A corrupt or hand-edited file** cannot make Relay recurse or crash: unknown nodes, empty splits
  and deep nesting are dropped before any widget is built, and a file with a foreign `version` is
  ignored wholesale.

### Deliberate non-goals / known gaps

1. **Maximised and full-screen state is not saved** — a maximised window comes back as a normal
   window of the same size.
2. **The tool-pane content is not restored**, only the pane and its path: a preview reopens the file,
   an explorer reopens the folder, a plan pane reopens the document; scroll positions and unsaved
   edits are not kept.
3. **Composer drafts and the per-pane queue are not saved.** Neither is the "human control" state.
4. **The pane's `preset` is restored through the global `provider/preset` setting**, which
   `configurePreset()` writes: restoring a window whose panes used different presets leaves the
   *last* one as the global default for new panes. The panes themselves are correct.
5. **One layout, no history** (owner decision 3). There is no "reopen closed window set".
6. **`--workspace` implies `--fresh`.** Passing a workspace explicitly always opens one new window;
   there is no way to say "restore *and* also open this directory".

## Automated tests

- `tests/windowstate_test.cpp` → ctest `windowstate`, **22 cases**: document round-trip, the geometry
  and screen fields, every pane field surviving the file for **both** engines (`relay` and `konsole`,
  which matters since commit 478b8a1 made the Relay engine the process default), 0600 permissions and
  directory creation, atomic replacement leaving no temp files, a missing file not being an error,
  five kinds of broken file being reported, usable/unusable nodes, depth rejection, `usableWindows()`
  dropping what cannot be rebuilt and clamping `current`, four screen-clamping cases, and the
  cwd → workspace → `$HOME` fallback.
- `./scripts/test.sh`: 376 tests pass (unchanged; no worker change was needed — `resume` already
  accepts `session_id` alongside a request `id`). `ctest --test-dir build`: 10/10.
- `cmake --build build` from a clean tree: no warnings.

## Live evidence

`docs/qa_evidence/2026-09-17-restore-windows/` — `NOTES.md` plus three harnesses:

- `drive.sh` (Xvfb + xdotool, isolated XDG dirs, `RELAY_KEYRING=off` and a literal non-key string as
  the provider key, so nothing real is touched and nothing reaches the network): five app starts
  covering the round trip, a resumed conversation, the missing-session note, both controls, the
  close-versus-quit rule and `--fresh`. Screenshots `implementer-01`–`implementer-14` and the saved
  `windows-after-run1.json` / `windows-after-run4.json`.
- `two-instances.sh` (offscreen): the second Relay does **not** write the layout —
  `two-instances.txt`.
- `broken-file.sh` (offscreen): a foreign-version and a malformed `windows.json` each give one
  stderr line and a normal single window — `broken-file.txt`.

## QA checklist

Run with your own profile, or copy `docs/qa_evidence/2026-09-17-restore-windows/drive.sh` and use an
isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`.

1. **Round trip.** Open two windows; in one make a vertical and a horizontal split and a second tab;
   `cd` each pane somewhere different; switch to the first tab. Quit. Start Relay with no arguments.
   Both windows, both tabs, the splits with their sizes, the current tab and every directory come back.
2. **Conversations.** Before quitting, run one agent turn in a pane. After the restart that pane prints
   `Session loaded: "…" · N turn(s)`, the open-task line if any, and a recap; the conversation is the
   old one (check `/tasks`, the context meter and Rewind). A pane that never had a turn prints
   `Previous conversation is no longer saved · starting a new one`. Delete a session file by hand and
   confirm the same note instead of an error dialog.
3. **Nothing re-runs.** No command from the old session is executed and the scrollback is empty.
4. **Models and modes.** Give two panes different models, efforts, plan mode and input mode. They come
   back per pane. Do the same with `--engine=konsole` for one pane and the Relay engine for another.
5. **Debounce.** Change the layout, wait two seconds, `kill -9` Relay, restart: the change is there.
   Change the layout and kill within a second: at most that change is lost. `windows.json` is 0600 and
   always parses.
6. **Close versus quit.** With two windows, close one (confirm the dialog) and quit the other: only the
   second comes back. Quit while both are open (close them one after the other without doing anything
   in between, or log out): both come back.
7. **Controls.** `relay --fresh` opens one window and leaves the file alone; the next plain start
   restores again. Palette → "Reopen windows on start" off: the file is deleted and the next start opens
   one window. Palette → "Start a fresh window set": the file is deleted, the status bar says so, a
   shortcut hint appears, the windows on screen are untouched, and the next start opens one window.
8. **Two instances.** With Relay running, start a second `relay` from a shell: it opens one plain window
   and says another Relay is running. Quit the second, change the first's layout, quit the first: the
   first's layout is what reopens. Confirm `windows.json` is never truncated or unparseable in between.
9. **Screens.** Save a layout with a window on a second monitor, unplug it, restart: the window lands
   fully on the remaining screen at its old size (or smaller if it did not fit). Under Wayland, only the
   size is honoured — that is expected.
10. **Directories.** Delete a pane's directory and restart: that pane opens in its workspace (or `$HOME`)
    and says nothing alarming.
11. **Ctrl+Shift+W is independent.** With the setting off, closing and restoring a pane, tab and window
    still works.
12. **Fresh profile.** A brand-new `XDG_DATA_HOME` opens exactly one window with one pane.
13. **Hand-edited file.** Put `{"version": 99}` or garbage in `windows.json`: Relay opens one window and
    says the saved layout was ignored, and does not crash or hang.
