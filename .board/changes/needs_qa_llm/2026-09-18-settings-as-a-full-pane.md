---
id: SP4N
type: work
status: needs-qa-llm
labels: [settings, gui, ux, palette, feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code session), 2026-09-18
rank: i
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist below under Xvfb (or a desktop) and records it under docs/qa_evidence/
source: 'owner, 2026-09-18: "make settings (ctrl shift a and the gear icon at top right) a full pane rather than a side bar. analyze it and compare it to the settings in warp / claude code / opencode, to make sure that it delivers a good experience" and "the pane could have sub-tabs if thats a good UX"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-settings-pane/], github: null, related: [XZZB, RBVK, 05J2]}
---
# Settings as a full pane, with sub-tabs and one search over settings and actions

## Request

Ctrl+Shift+A and the gear opened a 420 px strip floating over the right edge (the actions palette),
with settings two submenus deep and text and number settings edited through `QInputDialog`. The owner
wants a full pane instead, sub-tabs if that is good UX, and a check against Warp, Claude Code and
opencode.

## What the three reference apps do (checked 2026-09-18)

| | Where settings live | Sections | Search | Changing a value | Closing |
|---|---|---|---|---|---|
| Warp | A settings window (Ctrl+,), separate from the command palette (Ctrl+Shift+P) | Left list: Account, Appearance, Features, AI, Code, Keyboard shortcuts, Privacy, About; Appearance has sub-tabs (Themes, Text, Window, Prompt, Input) | Yes, over the settings | Instant, no Save | Close the window |
| Claude Code | `/config` and `/status` open one tabbed panel (Status, Config, Usage) over the prompt | A short list of personal options, not every key; the rest is `settings.json` | No | Select a row, Enter or Space changes it, saved at once | Esc; focus and even vim mode return to the prompt |
| opencode | No settings UI: `opencode.json` and `tui.json` | — | The command list (ctrl+p) is searchable and carries toggles that persist; `/models`, `/themes` pickers | Enter on a command | Esc |

What Relay takes from each: Warp's search-first sections with instant apply and no Save button;
Claude Code's "Enter changes the highlighted row, Esc closes and focus goes back to the prompt";
opencode's one searchable list where actions and toggles sit together, so the muscle memory
"Ctrl+Shift+A, type, Enter" still runs an action.

## Behavior as implemented

- **A full pane.** Ctrl+Shift+A (`palette.open`), the gear and Ctrl+, (`app.settings`) open one
  Settings pane beside the focused pane, in the splitter layout like the explorer and the
  Switchboard. The same key on the pane closes it; so do Esc on an empty search and the ✕. Closing
  returns focus exactly where it was (vim in the terminal, or the prompt box). The pane is not saved
  with the layout; closing it as the last leaf of the last tab puts a terminal pane beside it first.
- **Sub-tabs.** General, Appearance, Models, Terminal, Agent, Voice, Privacy, Actions. ← → switch
  tabs while the search is empty. Long tabs have engraved headings (Agent: Instructions and skills /
  Turn limits; Voice: Capture / Model; General: Diagnostics).
- **Rows as controls.** Toggle rows flip when clicked anywhere on the row; choices are combo boxes;
  numbers are spin boxes; text fields commit on Enter or focus loss. A change rebuilds the pane and
  the rebuild keeps the tab, the scroll offset, the highlighted row and the focused control.
- **One search.** Typing filters settings rows and every action (including submenu entries such as
  Model › DeepSeek) into one list, best match first, each row saying where it lives. ↑ ↓ move a
  highlight, Enter changes or runs it. Running an action closes the pane and runs it against the
  pane that had focus; a toggle (fast agent, plan mode, log detail…) runs in place.
- **Actions tab.** Every action with its keys: Recent first, then Agent, Terminal, Panes and tabs,
  Shortcuts, submenus opened inline. It replaces the Ctrl+? shortcuts dialog (`help.shortcuts` opens
  this tab and names the key that was pressed) and the Agents submenu (`agent.agentsMenu` opens it
  scrolled to Agents).
- **Rows that were missing** from the old window are now in it: Show tool output, Desktop
  notifications, Log detail, Log folder (General), Stop a silent model after (Agent).
- The palette overlay, the compact Settings dialog and the `set:`/`menu:settings` palette entries are
  removed. The shortcut presets are unchanged (`palette.open` keeps its key in each preset).

## Implementer check (not a QA verdict)

`tests/settingspane_test.cpp` (12 tests) and the full ctest suite pass. Xvfb run with an isolated
`XDG_CONFIG_HOME`: see `docs/qa_evidence/2026-09-18-settings-pane/` (implementer screenshots of
the pane beside a terminal, the search mixing settings and actions, the Actions tab, and the
Relay Light theme).

## QA checklist

1. Ctrl+Shift+A opens the Settings pane beside the focused pane, with the search focused; the tab
   title reads "Settings". Ctrl+Shift+A again closes it and the prompt box has focus.
2. The gear at the top right does the same; Ctrl+, opens it on the General tab.
3. With vim running in native input, Ctrl+Shift+A opens the pane without resizing vim's text
   (the splitter halves the pane; vim redraws) and Esc returns focus to vim: `ihello` then `:wq`
   writes the file.
4. Type `deep`: the list shows "Model › DeepSeek …" (with a stored key) under Agent; Enter closes
   the pane and switches the model. Type `thinking`: the General row appears with a checkbox; Enter
   flips it; the row stays highlighted and the tab does not scroll back to the top.
5. ← → in an empty search move between tabs and wrap; ↑ ↓ then Enter on the Appearance tab opens
   the Theme combo; choosing a theme restyles the window at once.
6. Actions tab: Recent shows the last things run; every action shows its keys; clicking "New tab"
   closes the pane and opens a tab.
7. Ctrl+? (or F1) opens the Actions tab and the status bar names the key that was pressed.
8. Clicking anywhere on a toggle row flips it. A spin box arrow writes once per click; typing a
   number then Tab writes once.
9. In a window with one tab and only the Settings pane, Esc leaves a terminal pane, never closes
   the window.
10. The pane is not restored after restart (Reopen windows on start = on).
