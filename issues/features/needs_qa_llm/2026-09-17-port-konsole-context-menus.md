---
id: X2F1
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzx2
created: '2026-09-17'
acceptance: an engine pane's right-click menu covers the Konsole items worth keeping, with the dropped ones listed in the card
source: '`issues/feature_intake.txt`, 2026-09-17: "compare the right click context menus we had in the konsole engine to see if there is anything we should bring in here."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-ux-batch2/'], related: [], github: null}
---
# Bring the useful Konsole context-menu items to the engine panes

Compare KonsolePart's right-click menu with the engine pane's and port what is worth having: copy, paste,
select all, clear scrollback and reset, search, open link / copy link address, open file at this path,
zoom in/out/reset, change profile bits, "Save output as…", and split/close pane. Keep Relay's own entries
(open the turn, take control, tasks) in the same menu, and list anything deliberately dropped.

## What the two menus were

**KonsolePart** (as shipped, captured in the evidence folder's first run): Copy · Paste · Open File Manager ·
Open Folder With ▸ — Set Encoding ▸ — Find… · Scrollback ▸ — Read-only · Allow mouse tracking — Switch
Profile ▸ · Edit Current Profile… — Close Session.

**Relay's engine** (`TerminalView::contextMenuEvent`): Copy · Paste · Select All — Find… · Clear Scrollback.

Neither had any of Relay's own actions.

## Implemented

### One menu, both engines (`src/TerminalBackends.h/.cpp`, `src/main.cpp`)

`relay::terminalContextMenu(TerminalMenuState)` returns the entry list as data — ids, labels, enabled flags
and `"-"` separators — so the list can be checked without a window or a shell. `Pane::showTerminalMenu()`
turns it into a QMenu and puts the live keymap text on the entries that have a key, so the menu teaches the
shortcuts (WARP.md).

In order: **Open last agent turn · Take control `Ctrl+H` · Tasks `Ctrl+Shift+K`** — Copy · Paste · Select all
— *(Open link · Copy link address · Open "<name>" when the pointer is over one)* — Find… `Ctrl+F` · Clear
scrollback · Clear scrollback and reset · Save output as… — Zoom in · Zoom out · Reset zoom — New pane to the
right `Ctrl+E` · New pane below · Close pane `Ctrl+W`.

Relay's own entries come first, because they are why this pane is not just a terminal. "Open last agent turn"
appears only when a turn has finished. The link entries appear only when the pointer is over a link or an
existing path.

- **Clear scrollback and reset** clears the history and then writes RIS (`ESC c`) into the emulator and asks
  the shell to redraw its prompt — both engines can do it (`DisplayInjection`).
- **Save output as…** writes the scrollback and the visible screen as plain text.
- **Find…** opens Relay's own find bar (which searches the conversation *and* the scrollback), not the
  engine's, so both engines offer it and the result is the same one people get from Ctrl+F.
- **Copy** is greyed when the engine can answer "is anything selected?" and nothing is. KonsolePart cannot,
  so its Copy stays live and does nothing without a selection — a wrongly greyed Copy would be worse.

### Where the right-click is taken

Both are caught in `Pane::eventFilter`, which sits on the application, so the menu replaces the engine's
before the engine ever sees the event.

- **Relay's engine** sends a proper `QEvent::ContextMenu`, and withholds it while a program is reading the
  mouse, so that is the event taken.
- **KonsolePart** never sends one — its display pops its menu straight out of the mouse press — so for
  Konsole panes the right-button **press** is taken instead. Consequence, on purpose: a program reading the
  mouse does not see a right-click in a Konsole pane. Engine panes are unaffected.
- Real widgets inside the terminal (the engine's Find bar) keep their own menus.

### New engine plumbing

- `TerminalBackend::linkAt(pos, line, column)` — the OSC 8 link, URL or existing path under a point, empty by
  default. `VTermBackend` implements it through the new public `TerminalView::linkAtPoint()`, which is the
  hit-test Ctrl+click already used. KonsolePart keeps the default, so it never offers the link entries.
- `TerminalBackend::zoom(step)` (+1 / -1 / 0 = reset) and the `FontZoom` capability. The engine drives
  `TerminalView::zoomIn/zoomOut/resetZoom`. `KonsoleBackend` probes its TerminalDisplay for
  `increaseFontSize()` / `decreaseFontSize()` by name and reports `FontZoom` only when they are there.

### Deliberately dropped from Konsole's menu

| Konsole entry | Why |
|---|---|
| **Open File Manager** | "Reveal in file manager" belongs to the explorer's menu, where there is a file to reveal (card #D60R). A terminal pane's version would only ever open its cwd. |
| **Open Folder With ▸** | Same, and a "choose an application" submenu in a terminal menu is a lot of menu for a rare action. |
| **Set Encoding ▸** | Relay is UTF-8 throughout, in both engines; there is nothing to choose. |
| **Scrollback ▸** (Configure / Clear / Clear and Reset) | Flattened: Clear scrollback and Clear scrollback and reset are top-level, and the scrollback size is a profile/settings matter, not a per-right-click one. |
| **Read-only** | Relay's panes are already prompt-box-first: the terminal takes no keys until Take control. The menu offers Take control instead, which is the real control. |
| **Allow mouse tracking** | Konsole-only, and it would mean nothing in engine panes. |
| **Switch Profile ▸**, **Edit Current Profile…** | Konsole profiles are not Relay's model — appearance is Relay's own theme and settings — and the engine panes have no profiles at all, so this could never be "the same menu on both engines". Zoom is the one profile bit worth having per pane, and it is in the menu. |
| **Close Session** | Present as **Close pane**, which is Relay's version (pane, then tab, then window) and carries Ctrl+W. |

An entry an engine cannot do is left out rather than shown dead, so on this KF5 KonsolePart the menu is
missing **Save output as…** (it exports neither scrollback nor screen text) and the three **Zoom** entries
(no font-size slots on its display). Everything else is identical in both menus, in the same order — there is
a test for exactly that.

### Tests

`tests/backends_test.cpp` (`backends` group, no new ctest group): Relay's entries come first and "Open last
agent turn" drops out with no turn; every ported Konsole item is present; the Konsole menu is the Relay menu
minus exactly `saveOutput`, `zoomIn`, `zoomOut`, `zoomReset` with the shared entries in the same order; Copy
is greyed only when the engine knows the selection; the link and file entries appear only with something
under the pointer and the file entry names the file; and across all 256 state combinations no menu starts,
ends or doubles a separator and every entry has a label.

### Evidence

`docs/qa_evidence/2026-09-17-ux-batch2/`: `implementer-relay-13-terminal-menu.png` and
`implementer-konsole-13-terminal-menu.png`, plus the README, which also records what Konsole's own menu
looked like before.

Build: `cmake --build build` with no new warnings. `./scripts/test.sh` 508 passed; `ctest --test-dir build`
16/16.

### One unrelated fix carried along

`src/main.cpp` used `qobject_cast<ChromeButton *>` on a tab's close button. `ChromeButton` has no `Q_OBJECT`
of its own, and Qt5's `qobject_cast` rejects that at compile time, so `main` did not build against Qt5/KF5 at
all. Changed to `dynamic_cast`, which is what the rest of the file uses for these types.

## QA checklist

1. Right-click in a terminal pane with **Relay's engine**: the menu is Relay's, not the engine's five-item
   one. Check every entry against "Implemented" above, and that Ctrl+H, Ctrl+Shift+K, Ctrl+F, Ctrl+E and
   Ctrl+W are shown beside their entries and match the current preset.
2. Right-click in a **KonsolePart** pane (`--engine=konsole`): the same menu, not Konsole's (no "Open File
   Manager", "Set Encoding", "Switch Profile", "Close Session"). Only Save output as… and the three Zoom
   entries should be missing — and if the installed Konsole does have font-size slots, Zoom should be there
   and should work.
3. Select some terminal output and right-click: **Copy** is live on the engine and copies. With nothing
   selected it is greyed on the engine and live-but-harmless on Konsole.
4. **Paste** with something in the clipboard, at a shell prompt: it arrives. **Select all** selects the
   scrollback.
5. **Find…** opens Relay's find bar and finds text from the scrollback and from the conversation.
6. **Clear scrollback**: the history goes, the current screen stays. **Clear scrollback and reset**: the
   screen clears too, the cursor is home and the prompt is redrawn. Do both while `vim` is running and after
   quitting it — the pane must not be left in a broken state.
7. **Save output as…**: run a few commands, save, and check the file holds the scrollback and the visible
   screen. Cancel must write nothing.
8. **Zoom in / Zoom out / Reset zoom** on an engine pane: the font changes and the grid re-flows; reset
   returns to the profile size. On a Konsole pane where the entries are absent, nothing is lost.
9. `printf 'https://example.com\n'`, then right-click on the URL in an engine pane: **Open link** and **Copy
   link address** appear; the link opens in a browser and the address pastes. `ls` a real file, then
   right-click its name: **Open "<name>"** appears and opens it in a preview pane. Right-click on plain text:
   none of the three appears.
10. **Open last agent turn** appears only after an agent turn has finished, and opens that turn's pane.
    **Take control** hands the keyboard to the terminal. **Tasks** shows and hides the task list.
11. **New pane to the right / New pane below / Close pane** from the menu do the same as the keys, including
    the "← ↑ ↓ to place" window on "New pane to the right" (card #78BN).
12. Right-click inside the engine's **Find bar** text field: the ordinary text-editing menu, not Relay's.
13. Run a full-screen program that reads the mouse (`htop`, or `vim` with `:set mouse=a`) and right-click:
    in an **engine** pane the program gets the click and Relay's menu stays away; in a **Konsole** pane
    Relay's menu appears instead — that is the documented trade-off, so confirm it is acceptable rather than
    treating it as a bug.
14. Right-click, then press Escape: the menu closes and nothing happens. Right-click in one pane while
    another is focused: the menu must act on the pane that was clicked.
