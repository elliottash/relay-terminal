# #SEJ2 — Ctrl+Enter / Shift+Enter in file panes: live evidence

Run of **2026-09-23**, against `main` at `5c3c6b1e` ("Teach Shift+Enter from the
explorer menu's Open external #SEJ2"; the chords themselves landed earlier in
`5dfec93d`). The binary was the tree of that commit built by `scripts/land.py`'s
gate in `/tmp/claude-1000/land/sej2/verify/` (`cmake --build … --target relay`),
which is the exact tree the commit put on `main`.

## What ran

`./drive.sh <relay binary> <out dir>` — starts Xvfb, sandboxes `$HOME`, drops a
stub `xdg-open` on `PATH` that logs what the desktop is asked to open, launches
Relay on a folder holding `note.txt` and `readme.md`, then drives the UI and
checks each step by screenshot OCR (`tesseract`) and by the bytes on disk.
`phases.sh` holds the steps; `notes.txt` is the run's verdict: **17 passed,
0 failed**. `xdg-open.log` is the desktop's side of both Open-external paths.

## The claims, as checked

| # | Claim (from the card's Plan) | Evidence |
|---|---|---|
| 01 | The explorer is up over a two-file folder | `01-explorer.png` |
| 02 | **Enter previews read-only** — content shown, no Save button | `02-enter.png` |
| 03 | **Ctrl+Enter opens it editable** — typed text lands, Save appears | `03-editable.png` |
| 04 | **Ctrl+S saves** — "Saved" notice, `scratch line` on disk | `04-saved.png` |
| 05 | **Dirty close asks** — Save/Discard/Cancel dialog; Discard keeps disk | `05-dirty-close.png` |
| 06 | Enter again is **read-only with the saved bytes**; the discarded edit is gone | `06-readonly.png` |
| 07 | The **✎ Edit button** opens editing and teaches Ctrl+Enter | `07-edit-button.png` |
| 08 | Menu **Open external** teaches Shift+Enter and hands the file to the desktop | `08-menu.png`, `08-open-external.png`, `xdg-open.log` |
| 09 | **Shift+Enter hands readme.md out** the same way | `xdg-open.log` |

Remote (`ssh://`) rows are not covered here: the sandbox has no ssh host. The
code path (remote opens editable already, so Ctrl+Enter acts as Enter there)
is covered by `tests/filepanes_test.cpp` and the class comment in
`src/FilePanes.cpp`.

## Two harness lessons, learned the hard way (probes in the card thread)

- **The explorer opens a row on a single click**, so a row click is an
  activation, not a selection. The drive navigates by keyboard instead — click
  the Filter line, `Down` to the first match — which is also closer to how the
  chords' users arrive.
- **Geometry must be read from a shot of the current layout.** Panes open and
  close between phases; a Filter coordinate cached one phase earlier points
  into a neighbouring pane. `to_first_row` re-shoots before every navigation.

Neither is an app fault: the same chords were also verified through both the
XSendEvent (`xdotool key --window`) and the XTEST (real-input) delivery routes
on a genuinely selected row.

## Reproducing

```
cd docs/qa_evidence/2026-09-20-edit-here
./drive.sh /path/to/relay /tmp/out    # needs Xvfb, xdotool, import, tesseract
cat /tmp/out/notes.txt
```

Unit-level cover for the same chords: `ctest --test-dir build -R '^filepanes$'`
(`shiftEnterOnFileGoesToTheDesktop`, `ctrlEnterOnFileOpensItEditable`,
`openExternalFromTheMenuTeachesShiftEnter`).
