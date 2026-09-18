---
id: K7RY
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: settings
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Ctrl+Shift+A opens the Settings pane on Actions, Ctrl+Shift+O (and the gear, and Ctrl+,) opens it on the options, each key moves an open pane to its side and closes it from its own side; Ctrl+Shift+Y runs /resume; none of the three has a plain-Ctrl twin; no preset reports a key conflict.'
source: 'owner, in session 2026-09-18: "i want ... ctrl shift o for options ... ctrl shift y for /resume (same as warp) ... then ctrl shift a for actions (and actually remove ctrl o and ctrl y on their own, to avoid confusion)"'
links: {plans: [], commits: [ae0fead], evidence: ['docs/qa_evidence/2026-09-18-actions-and-options-keys/'], related: [], github: null}
---
# Actions, Options and Resume each get one Ctrl+Shift key

## Report

One key (Ctrl+Shift+A) opened the Settings pane for two different jobs: changing what persists
(options) and doing something now (actions: resume a session, open the Switchboard, set the model,
new window/tab/pane, rewind). The owner wants the two told apart, starting with the keys.

## What changed

- **Ctrl+Shift+A — Actions** (`palette.open`). Opens the pane on the Actions tab.
- **Ctrl+Shift+O — Options** (`app.settings`; Ctrl+, stays as a second key in the Relay preset).
  Opens the pane on General. The gear in the title bar is Options too: its tooltip and its
  "Next time" hint now name this key.
- Either key, pressed while the pane shows the *other* side, moves the pane there; pressed on its
  own side it closes the pane (`toggleSettingsPane(bool actions)` in `src/main.cpp`).
- **Ctrl+Shift+Y — `/resume`** (`agent.resume`), Warp's key for its conversations menu. `/resume`
  typed by hand now shows the "Next time: Ctrl+Shift+Y" hint (WARP.md's standing rule).
- No plain Ctrl+O or Ctrl+Y: they are operate-and-get-next and yank in the shell.
- `conversations.open` gave up Ctrl+Shift+O and is unbound; `/conversations` and Actions ›
  Conversations… still open it.
- Presets: `app.settings` is `Ctrl+Shift+O` in Warp, VS Code and Konsole (it was unbound, because
  Ctrl+, is `keybindings.edit` there). The Warp preset no longer puts `agent.newChat` on
  Ctrl+Shift+Y, which would collide with resume.
- The `?` help card lists actions, options and resume with their keys, in place of the
  conversations row.

Not in this change: moving rows between the two sides (the verb-shaped button rows under General
and Appearance, the shortcut preset rows on the Actions tab) and giving Actions a slimmer list of
its own. That is the next step of the same split.

## QA checklist

1. Fresh profile. Ctrl+Shift+O: the pane opens on General. Ctrl+Shift+O again: it closes and focus
   returns to where it was.
2. Ctrl+Shift+A: the pane opens on Actions with the search box focused; type `resu`, Enter runs
   Resume session….
3. With the pane open on Actions, Ctrl+Shift+O moves it to General without closing; Ctrl+Shift+A
   moves it back.
4. The gear opens General; its tooltip reads `Options  (Ctrl+Shift+O)`.
5. Ctrl+Shift+Y opens the resume picker (with a provider configured). Plain Ctrl+Y at a shell
   prompt still yanks; plain Ctrl+O does not open anything of Relay's.
6. Both keys work while vim owns the terminal.
7. Switch the shortcut preset to Warp, VS Code, Konsole in turn: no conflict notice; Ctrl+Shift+O
   opens options in each.
8. `?` in an empty prompt box: the card shows the three keys.
