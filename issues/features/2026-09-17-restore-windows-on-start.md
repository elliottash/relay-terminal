# Restore windows, tabs, panes and their conversations on start

- **Status**: open
- **Component**: gui, worker
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: quitting Relay with several windows, tabs and split panes and reopening it brings back the same layout, each pane in its old directory with its agent conversation reattached; a fresh profile still opens a single pane

- **Assignee**: unassigned
- **Source**: owner in chat, 2026-09-17: "i want it to save and persist, and when you re-open, its back to where you were, like in warp"

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

## Open questions
1. Restore silently on start (recommended, Warp-like), or show a "Restore previous session?" prompt the first time?
2. Should a restored pane re-run nothing at all (recommended) or optionally re-run its last command?
3. Keep one saved layout, or a short history of layouts ("Reopen closed window set")?
