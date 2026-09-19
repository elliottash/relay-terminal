# #916B — the project picker, the tab chip, the known-projects list, the Sessions project chooser and the board's folder action: implementer evidence

Implementer: Claude Fable 5.1 (Claude Code session `picker`), 2026-09-19. These are implementer
shots, not QA verdicts; the card's checklist is untouched.

`drive.sh [build-dir]` runs Relay under Xvfb with an isolated HOME / XDG_* / TMPDIR and
`RELAY_KEYRING=off`, started in a loose `~/Downloads`. The fixture is a registry with two known
projects — `widgetworks` (a git checkout with a board in `.switchboard/` and one card) and `notes`
(no board) — one declined path, and three saved agent sessions (two under `widgetworks`, one of
them from its `sub/` directory, and one in `Downloads`) for the Sessions pane. No provider key is
needed: the picker, the chip, the Options list and the folder rename go through the pane's worker
without a model. `implementer-disk.txt` is what the fixture looked like afterwards;
`implementer-tests.txt` the unit runs.

## What the shots show

| Shot | What it shows |
|---|---|
| `implementer-01-picker.png` | Ctrl+Shift+S in `~/Downloads` (no project, no candidate): the **Projects** pane beside the terminal — "＋ Initialize new project here  ~/Downloads" first, then `widgetworks` ("opened its Switchboard · an hour ago") and `notes` ("/card was run in it · 2 days ago"), most recently attached first |
| `implementer-02-picker-filtered.png` | Typing `wid` keeps the init row on top and selects `widgetworks` |
| `implementer-03-attached-chip.png` | Enter: the tab is attached (reason `picker`) and its Switchboard opens with the fixture card; the tab wears the chip **widgetworks** at the left of its label |
| `implementer-04-gear-folder.png` | The gear after the section checkboxes: the section editor now says "This board is kept in .switchboard/, hidden from directory listings and from ripgrep-based agents." with **Show this board's folder** |
| `implementer-05-folder-shown.png` | After the click: the notice ".switchboard/ is now switchboard/ (shown, git mv)", the board still shows its card; `implementer-disk.txt` lists `switchboard/` under widgetworks and `git status` shows the two `R` renames |
| `implementer-06-detached.png` | One click on the chip: the chip is gone, "This tab is no longer attached to widgetworks.", the hint "Next time: Ctrl+Shift+A · then “Detach this tab”", and the Switchboard pane stays open |
| `implementer-07-sessions.png` | The Sessions pane with the new **Project** chooser after Kind ("Any project"); "This project" lists the one session saved in `Downloads` |
| `implementer-08-sessions-project.png` | `widgetworks` chosen: the two sessions whose workspace is that folder or its `sub/` ("2 session(s) in all projects"), and the scope menu followed the worker to "All projects" |
| `implementer-09-init-here.png` | Ctrl+Shift+S again in `~/Downloads` (the Switchboard pane had been closed), Enter on the init row: "Switchboard created in …/Downloads/.switchboard/ · git repository initialized", the Switchboard opens, the chip says **Downloads**; `implementer-disk.txt` shows `.switchboard/board.yaml` and `.git` |
| `implementer-10-options-known.png` | Options › search "known": `Downloads` ("known because chosen in the project picker, just now"), `widgetworks`, `notes` ("… · no Switchboard yet"), each with **Remove**; the declined `widgetworks-old` with **Undo** |
| `implementer-11-options-removed.png` | Enter on the first row (its Remove): the row is gone, the notice "Forgot Downloads · its files, Switchboard included, are untouched; an attached tab stays attached"; `implementer-disk.txt` shows the registry without it and `Downloads/.switchboard`, `.git` still there |

Not covered here: `/card <text>` through the picker (it needs a model to answer nothing, but a
provider key to send), and the picker's "Default project for loose cards" preselection, which
`tests/projectpicker_test.cpp` covers offscreen (`theDefaultProjectIsPreselectedWhenSet`).
