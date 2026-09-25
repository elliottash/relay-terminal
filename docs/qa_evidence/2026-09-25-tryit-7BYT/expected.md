# #7BYT expected results (sealed — the person compares against this only after trying)

1. Alt+click on the underlined `/…/proj/src/boom.py` path (or Alt+Enter on it in the link walk):
   nothing opens; the pane's prompt box gains `@src/boom.py:3 ` at its cursor, a toast says
   `Added to this prompt: …`, and a shell-mode prompt box flips to agent mode (model picker
   showing). The focus stays in the pane you clicked.
2. Alt+drag across two or more lines of output: the selection highlights while dragging and, on
   release, the selected text sits in the prompt box as a fenced block (``` … ```), with the same
   toast. A single-line drag inserts the line as-is.
3. Ctrl+click on the `boom.py` line of the `ls src` output (or a folder path anywhere): the
   pane's shell runs `cd` into `src` — the prompt becomes `…/proj/src$`. No menu appears.
4. Right-click on any path: the menu shows `Add to prompt  Alt+click` (working) and
   `Navigate …  Ctrl+click`; plain click still opens the folder in the explorer / the file in
   Relay, and Shift+click still opens the file manager.
5. Ctrl+Alt+drag still makes a rectangular selection (no toast, nothing added).
