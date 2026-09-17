# Pane and tab buttons, moving panes and tabs

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (GUI F1 worker), 2026-09-17
- **Source**: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
- **Workstream**: windows

## Behavior as implemented

- Hovering a pane (terminal or tool) shows a button row in its top-right corner: ⠿ drag grip, split right, split down, move to new tab, close (same as pane.close, restorable).
- Move to new tab keeps the live shell, worker and conversation (the old splitter collapses as on close). Tabs: "+" after the last tab; ⧉ on the hovered tab and "Move to new window" in the tab context menu move the tab with its live panes to a new window.
- Dragging the grip onto another pane shows a drop zone on the nearest edge; dropping docks the pane there (also across windows); dropping on a tab bar makes it a tab. Esc cancels.
- Keymap: `pane.moveLeft/Right/Up/Down` default Ctrl+Alt+arrows: swap with the adjacent pane in the same split, otherwise dock beside the neighbor. The Warp preset keeps Ctrl+Alt+arrows for pane focus and leaves move unbound. GNOME/KDE may grab Ctrl+Alt+arrows for workspaces. `pane.moveToNewTab` and `tab.moveToNewWindow` have no default key. Palette entries for all, with aliases.
- Pane callbacks look up their current window when called, so moved panes keep working.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-pane-tab-buttons-moving/`: button row on hover (`implementer-01`); Ctrl+Alt+Left swapped two panes, shells kept their output (`implementer-02`); dragging the grip showed the bottom-edge drop zone (`implementer-03`) and dropped the pane below with its history (`implementer-04`); move to new tab (`implementer-05`); ⧉ moved the tab to a new window, where `echo STILL-WORKS` and Ctrl+T worked (`implementer-06`).

Not verified live: dropping onto a tab bar, dragging between two windows (Xvfb had no window manager), Esc during a drag, tool panes (explorer/plan) moved to another window.

## QA checklist

1. Split twice; use each button; restore a closed pane with Ctrl+Shift+W.
2. Start `sleep 100` in a pane and an agent turn in another; move both (buttons, drag, Ctrl+Alt+arrows): nothing restarts.
3. Drag a pane onto each edge of another pane and onto the tab bar; press Esc mid-drag.
4. Move a tab to a new window; in the new window use /plan, @ file opening and a subagent transcript: panes open in the new window.
5. Switch to the Warp preset: Ctrl+Alt+arrows focus panes and do not move them.
6. Drag a pane from one window onto a pane in another window.
