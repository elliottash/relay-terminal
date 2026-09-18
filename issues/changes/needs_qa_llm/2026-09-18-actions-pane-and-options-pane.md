---
id: V4NA
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: settings
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code, session relay-terminal-ad), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Ctrl+Shift+A opens an Actions pane that is one filterable list with no tabs; Ctrl+Shift+O opens an Options pane that holds only what persists; either key swaps an open pane in place; an option found from Actions is a row that opens Options on it; no verb is a button row in Options; the pane sets paneType "actions"/"options" for the pane chrome.'
source: 'owner, in session 2026-09-18: "help me distinguish the current options menu from an actions menu that we still need ... these are not persistent settings but things that matter now and you might choose regularly or change back and forth ... and in turn, we can move stuff out of options and into actions"; then "do it, and coordinate with this agent" (session relay-terminal-93, cards #SPBN #XM0T #R6J0).'
links: {plans: [], commits: [246847c], evidence: ['docs/qa_evidence/2026-09-18-actions-pane-and-options-pane/'], related: [K7RY, SPBN, XM0T, R6J0], github: null}
---
# The Actions pane and the Options pane

## Report

One Settings pane did two jobs. Its last tab, "Actions", listed everything runnable under two
option rows, and several "options" were verbs with a button. The owner wants the two told apart:
things you do now and switch back and forth (resume a session, the Switchboard, the model, a new
pane, rewind, opening options) against what persists. #K7RY gave each its key; this is the split.

## The line

An **option** persists: a default, written to QSettings, true in every pane after a restart. An
**action** is something you do now, to this pane, conversation or window. Pairs show it: "Default
reasoning effort" is an option, "Reasoning effort" for this pane is an action. A button row stays
in Options only when it opens the editor of something that persists (API keys, Model roles,
Instructions, Skills, keybindings.json).

## What changed

- `SettingsPane` has a `Mode`. **Actions**: search box, Recent, then every action under its
  section with its keys; no tabs. **Options**: a tab per section, rows as controls; no action list.
  `setMode()` swaps in place, so the other key never opens a second pane; "Options…" chosen in
  Actions swaps too.
- The pane has no title of its own any more: the pane header names it, and `ToolPane::title()` and
  the `paneType` property ("actions" / "options", agreed with session relay-terminal-93 for the
  pane chrome of #SPBN) follow the mode.
- Search covers both catalogs in either mode, the pane's own kind first. In Actions an option is an
  "Options › Appearance › Theme" row; Enter opens Options on that tab with that row highlighted.
  Several words are matched one by one in any order ("appearance theme"). Letters scattered across
  a sentence no longer count as a match ("reset" found "Reasoning effort › low").
- Moved out of Options into a new **Relay** group in Actions: Reset shortcut hints, Open the log
  folder, Reload themes, Open your themes folder; plus **Options…** itself. "Start a fresh window
  set" and "Reload keyboard shortcuts" were in both and are now actions only.
- The Options tab "Actions" is **Keyboard**: Shortcut preset, Shortcuts inside programs, Edit
  keyboard shortcuts…, and the note on what the mouse does.
- Ctrl+? opens the Actions pane.
- Every user-facing "Settings › X" is "Options › X": GUI status lines, two worker messages, README,
  architecture doc, site.

## Deliberately left

- **"Edit keyboard shortcuts…" stays in Options › Keyboard as well as in Actions.** I first listed it
  as a verb to move; by the rule above it is the same kind of row as "API keys…", so it stays.
- **Theme is an option only.** Light/dark is something people do flip back and forth, so a Theme
  submenu in Actions is defensible; but it persists and applies everywhere, and "theme" typed in
  Actions reaches it in one Enter. Owner's call if that is one step too many.
- **Pane colours option** (`appearance/pane_colours`, asked for by relay-terminal-93): written, but
  it calls `PaneChrome::refreshAll()`, which was not on main when this was committed. It follows in
  its own commit once that lands.

## QA checklist

1. Ctrl+Shift+A: one list, no tab bar, footer says "Enter runs". Recent is on top after you have run
   something.
2. Type `resu` Enter: the resume picker. Type `switch` Enter: the Switchboard. Type `model`: the
   stored models, the current one ticked.
3. Type `theme`: Reload themes, Open your themes folder, then "Options › Appearance › Theme". Enter on
   the last: Options, Appearance, Theme highlighted, search empty and focused. The theme did not change.
4. Ctrl+Shift+A in Options swaps to Actions; Ctrl+Shift+O in Actions swaps to Options; the tab title
   and pane header follow each time. There is never a second pane.
5. Each key closes its own pane; Esc clears a search, then closes; focus returns to where it was
   (try from vim in native mode).
6. Options › General has no Reset or Forget button and no Log folder row; Appearance has no Reload
   or folder row; the last tab is Keyboard.
7. In Options, type `new pane`: the action is found below any option and Enter runs it.
8. `appearance theme` and `theme appearance` both find the Theme option; `reset` does not list
   "Reasoning effort".
9. Ctrl+? opens Actions and says which key you pressed.
10. Nothing in the app says "Settings ›".
