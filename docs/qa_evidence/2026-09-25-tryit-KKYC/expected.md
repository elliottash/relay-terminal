# Sealed expected results — #KKYC try-it

Staged window: `open-relay.sh` under the run's `scratch/tryit/kkyc-clicks/`. The terminal shows
`ls` output with `reports` (folder), `notes.md`, `budget.csv` (files).

Folder `reports`:

| input | result |
|---|---|
| left click | the explorer pane opens beside the terminal showing the folder's contents |
| right click, Ctrl+click | a menu at the pointer: Open in explorer · Navigate here · Open in file manager · Copy path |
| Alt+click | the pane's shell runs `cd` into the folder; the prompt shows the new directory |
| Shift+click | the system file manager opens on the folder |

File `notes.md`:

| input | result |
|---|---|
| left click | the file opens in a preview pane |
| right click, Ctrl+click | a menu: Open · Edit · Navigate to its folder · Open with the default app · Copy path |
| Alt+click | the shell `cd`s to the folder holding the file |
| Shift+click | the file opens with its default app |

Keyboard equivalents during a link walk (Ctrl+Shift+L, arrows): Enter = left click,
Alt+Enter = navigate, Shift+Enter = external; the hint line under the pane names them.
Ctrl+Enter still edits a file directly during a walk.

Failure looks like: a folder click opening a menu on plain click, Alt+click starting a rectangular
selection instead of navigating, Ctrl+click on a file opening the editor instead of the menu, or
`cd` typed into a running program.
