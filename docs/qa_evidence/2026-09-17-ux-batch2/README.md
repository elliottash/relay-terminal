# UX batch 2 — implementer run, 2026-09-17

Four cards in one pass: the explorer's right-click menu and toggle (**#D60R**), single-click opening
(**#0C7V**), one key plus an arrow for a new pane (**#78BN**) and Relay's own terminal right-click
menu on both engines (**#X2F1**).

Run by the implementing Claude session under Xvfb, Qt5/KF5 build, `xdotool` for input:

- Relay's own engine: `--engine=relay`, display `:137`
- KonsolePart: `--engine=konsole`, display `:138`

Each run used a fresh `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_RUNTIME_DIR` under a private
temporary directory and `RELAY_KEYRING=off`, so nothing touched the real profile or keyring. The
screen was 1500x950 and the window 1320x855 at 0,0. **There is no window manager on either
display.** The workspace held `alpha/` (with `readme.md`), `beta/` and `notes.txt`.

Screenshots are of the Relay window id (`xdotool search --name '^Relay'` → `import -window <id>`),
not the root window. The three shots that show a popup — a context menu or the Settings window —
grab the whole screen instead (`import -window root -screen`), because a menu and a dialog are
separate top-level windows and would otherwise be a black hole in the Relay window's own grab. The
grab for those is started *before* the menu opens, because a Qt menu holds a pointer grab and an
`import` launched afterwards hangs.

| File (`implementer-<engine>-…`) | Shows |
|---|---|
| `01-baseline` | One pane, no explorer. |
| `02-explorer-open` | **Ctrl+B** opened the explorer pane on `…/ws`, listing `alpha`, `beta`, `notes.txt`. |
| `03-explorer-menu-folder` | Right-click on `alpha`: Open · Navigate here — Copy path · Copy relative path — Reveal in file manager — New file… · New folder… · Rename… · Delete… — Set as agent workspace. |
| `04-explorer-menu-file` | Right-click on `notes.txt`: Open · **Open in a preview pane** · Navigate here, then the same copy/reveal/create/rename/delete block. No "Set as agent workspace" for a file. |
| `05-explorer-menu-empty` | Right-click on the empty space below the rows: Navigate here — Reveal in file manager — New file… · New folder… — Set as agent workspace, all acting on the folder being shown. No Copy path / Rename / Delete, because nothing was clicked. |
| `06-single-click-folder` | A **single** left click on `alpha` walked into it (tab title `alpha`, listing `readme.md`). |
| `07-ctrl-click-selects` | **Ctrl+click** on `readme.md` selected the row and opened nothing — no preview pane appeared. |
| `08-explorer-closed` | **Ctrl+B** again closed the explorer; one pane is left. |
| `09-new-pane-hint` | **Ctrl+E** made a pane on the right, focused and running, with the transient "← ↑ ↓ to place" hint over it. |
| `10-placed-below` | **↓** pressed inside the two-second window re-docked that same pane below the one it came from — same shell, same prompt, no restart. |
| `11-late-arrow-ignored` | A new pane, then **←** pressed about three seconds later: the pane stayed on the right and the hint was gone, so the arrow behaved normally. |
| `12-settings-single-click` | Settings › General shows **Open items with a single click** (on), between "Recap when you come back" and "Reopen windows on start". |
| `13-terminal-menu` | Right-click in the terminal: Relay's own menu, not the engine's. |

## The terminal menu, engine by engine

`implementer-relay-13-terminal-menu.png` (Relay's engine):

> Take control `Ctrl+H` · Tasks `Ctrl+Shift+K` — Copy (greyed, nothing selected) · Paste · Select all
> — Find… `Ctrl+F` · Clear scrollback · Clear scrollback and reset · Save output as… — Zoom in ·
> Zoom out · Reset zoom — New pane to the right `Ctrl+E` · New pane below · Close pane `Ctrl+W`

`implementer-konsole-13-terminal-menu.png` (KonsolePart): the same list and the same order, minus
**Save output as…** (this KonsolePart exports neither scrollback nor screen text on KF5) and the
three **Zoom** entries (its TerminalDisplay has no `increaseFontSize` / `decreaseFontSize` slots to
probe). **Copy** is not greyed there, because KonsolePart cannot answer "is anything selected?" and
a greyed Copy would be wrong more often than a live one. "Open last agent turn" is absent in both
because no agent turn had finished; the link entries (Open link / Copy link address / Open <file>)
are absent because the pointer was not over one.

Before this change, `implementer-konsole-*` right-clicks produced **Konsole's** menu (Copy, Paste,
Open File Manager, Open Folder With, Set Encoding, Find, Scrollback, Read-only, Allow mouse
tracking, Switch Profile, Edit Current Profile, Close Session); the engine panes produced
TerminalView's five-item menu (Copy, Paste, Select All, Find, Clear Scrollback).

## Not covered here

- Actually running the destructive explorer entries (New file / New folder / Rename / Delete) and
  "Set as agent workspace": each opens a modal dialog, and the QA session should drive those.
- "Reveal in file manager": no `org.freedesktop.FileManager1` service on this display.
- "Open link" / "Copy link address" / "Open the file at this path": they need a link or an existing
  path under the pointer, which the engine hit-tests and KonsolePart cannot.
- Zoom in / out / Reset zoom on KonsolePart: the slots were not found on this build, so the entries
  are left out — a Konsole with them would show them.
- "Save output as…" writing a file (a file dialog), and the preview file picker's single-click
  behaviour (also a file dialog).
- Drag-selection in the explorer, and the placement window being cancelled by a mouse click.
