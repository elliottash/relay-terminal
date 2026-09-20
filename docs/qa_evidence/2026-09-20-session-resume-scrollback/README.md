# Reproduction: a session resumed from the session manager has no scrollback (#0TJ9)

`drive.sh` (Xvfb + xdotool, isolated XDG including `XDG_RUNTIME_DIR` — the layout lock lives in
`$XDG_RUNTIME_DIR/relay/windows.lock`, so without that the run never owns its layout and saves
nothing):

- **Run 1** prints in a pane (the input router sent the command line to the agent, which ran it
  as a tool call — cosmetic; the pane had terminal content either way) and quits through the
  window. `run1-state.txt`: `windows.json` pairs the pane's `session_id` with its `scrollback`
  id, and `state/scrollback/<id>.txt` holds the pane's text. Saving works — this is #SB7K.
- **Run 2** starts with `--workspace` (the saved layout is *not* restored — the old pane is
  gone, as it is days later), opens the session manager (Ctrl+Shift+Y, shot 03) and Shift+Enter
  on the session. Shot 04: a new pane shows "Session loaded: … · 1 turn(s)" and nothing above
  it. Shot 05: five PageUps change nothing — **no scrollback**.

Two layers, both measured:

1. **The resume path never looks the text up.** `Pane::openSavedSession` →
   `RelayWindow::openFork` → `createPane({cwd, workspace})`: the new pane's scrollback id is its
   own fresh token (`Pane.h` constructor), no `scrollback` key is in the spec, so
   `m_restoredScrollback` stays empty and nothing is replayed. The layout's
   session-id ↔ scrollback-id pairing is only ever consumed by a full layout restore.
2. **The text is gone soon anyway.** `run2-state.txt`: run 2's own first layout write pruned run
   1's scrollback file — `WindowManager::writeWindows` keeps only ids named by the live layout
   and the recently-closed list. A session whose pane has left the layout loses its saved text
   at the next layout write, so a fix that only teaches resume to look up the old id would find
   nothing in exactly the case the owner hit.

Screenshots: `01-run1-markers` (run 1 content), `02-run2-fresh`, `03-run2-manager`,
`04-run2-resumed` (the resumed session, empty above), `05-run2-paged-up` (PageUp: still
nothing). `relay-stderr.log` is both runs.

## The fix, driven: `drive-fix.sh`

Same isolation, and this time turns really run: no provider account, so the profile points a
local model endpoint at `stub-provider.py` on 127.0.0.1, which answers "alpha …" with sixty
ALPHA-MARKER lines and "bravo …" with sixty BRAVO-MARKER lines. Sixty because the card's symptom
was "i couldnt scroll back": a resumed pane has to hold more than a screen of text before PageUp
means anything. Shots are `fix-*`, the file findings are `fix-state.txt`, and the run exits 0.

**Run 1 — two conversations in one pane.** "alpha …" (`fix-01-run1-alpha`), then `/new`, then
"bravo …" (`fix-02-run1-bravo`), then the window is closed. On disk, beside the two session
files:

```
4324  …/f4d86bc83c82f3d1/4c12a7cc7a5e4b5ab113b1eaf2ea6ffe.json
2090  …/f4d86bc83c82f3d1/4c12a7cc7a5e4b5ab113b1eaf2ea6ffe.scrollback.txt
4325  …/f4d86bc83c82f3d1/5b018559a163469180ef01e68f42aca4.json
2195  …/f4d86bc83c82f3d1/5b018559a163469180ef01e68f42aca4.scrollback.txt

4c12…  ALPHA-MARKER lines: 0   BRAVO-MARKER lines: 60
5b01…  ALPHA-MARKER lines: 60  BRAVO-MARKER lines: 0
```

**(c) session B's file does not begin with session A's text** — two files, one per conversation,
each holding only its own markers. The text is keyed by the conversation now, not by the pane, so
neither file is pruned when the pane goes.

**Run 2 — resume, with the file.** A fresh start (the old pane is gone, as it is days later),
Ctrl+Shift+Y, Enter on the row. `fix-03-resumed`: the pane ends on

```
• ALPHA-MARKER line 60 of sixty
— end of the conversation's saved text; this shell is new —
~/project $
Session loaded: "alpha: say your markers" · 1 turn(s)
```

**(a) the saved text is above the "Session loaded" line**, under the conversation's own rule —
not the per-pane "previous shell" one, which would be untrue of text printed in another pane.
`fix-04-paged-up`: two PageUps reach the top of the block — the opening rule "— saved terminal
text from this conversation —", the `✦ alpha: say your markers` prompt, the `▸ stub` line and
ALPHA-MARKER line 01 onward. **PageUp scrolls**, which is the thing the owner could not do.

**Run 3 — resume with no file at all.** Every `*.scrollback.txt` is deleted first, standing in
for the conversations saved before this store existed, a crash, and a conversation made on the
phone. `fix-05-fallback`: **(b) the pane prints the transcript instead** — the same two rules,
with the ✦ prompt and the sixty reply lines drawn between them from the worker's
`conversation_get` answer (no `~/project $` and no `▸ stub` line: this is the conversation's
entries, not a terminal recording). `fix-state.txt` records `files left: 0` before the run.

Two things `drive-fix.sh` documents because they cost an evening each: the boundary a
conversation's text starts at must be measured against the *history above the screen*, never
against the screen (its row count is the grid height whatever is on it, so the mark is a
constant and every slice comes out empty); and `cleanup` must never `kill 0`, which is the whole
process group, the script included.
