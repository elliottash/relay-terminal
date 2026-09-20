# The Switchboard's delete, live (card #CYM9)

`drive.sh` builds a board of three fixture cards (Inbox: Alpha, Bravo with a thread comment;
Ready: Charlie), runs `build/relay` on it under Xvfb with an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`, and drives it with xdotool. Every claim is
checked by OCR of the screenshots (tesseract word boxes on upscales of the board pane) or by
reading the fixture card files — the PNGs carry what OCR cannot (the dialog's layout, the
notice's Undo button). `ocr.txt` is the receipt.

Run: `bash drive.sh [build-dir] [out-dir]` (defaults: `../../../../../build`, this folder).

## What was verified (ocr.txt lines in brackets)

- **The Del key on the selected card asks, then deletes.** The confirm dialog came up naming
  the card and its thread and the 30 s Undo [02, 03]; confirmed, the notice said
  "Deleted #NY1V", the card file was gone from disk and the row from the list [03].
- **Undo restores the card byte for byte.** Ctrl+Z inside the window: "Undone: #NY1V is back
  as it was.", the file back with `diff` clean against the pre-delete bytes, the row back [04].
- **The open card's ⌫ Delete button opens the same confirm** (the button is on the title row
  beside the ✎ Edit pencil) [05, 06]; confirmed once, the card *and its thread file* left the
  disk and the detail closed [07].
- **Cancel sends nothing.** Del on the open card, dialog up [08b], Escape: the card file
  intact and the card still open [09].
- **A removal from outside Relay closes the open card with the notice** — `rm` of the open
  card's file, the same `board_changed {removed}` wire path another pane's delete takes:
  "#NY1V was removed from the board." [10]. This pane's own delete does *not* get that
  notice — its "Deleted #ID · Undo" toast survives the removal events — which is the unit
  test's other half (`boardmodel_test.cpp`).

## The fault this run's earlier passes found and fixed

The first implementation confirmed twice on the button path — once in `CardDetail::remove()`,
once in the `BoardView::deleteCard` it handed off to — so a confirmed delete left a second,
unanswered dialog on screen (the live drive's OCR click answered the first; the unit test
hung on the second for want of an armed answer). The confirm now lives in `deleteCard` alone:
one question per delete, from the button, the Del key or the `m` menu alike.

## Notes

- The dialog is not an X window of its own under Xvfb (no WM): `confirm_delete` in `drive.sh`
  finds it by OCR of its informative text ("committed."), at a fixed offset from the Delete
  button — the buttons themselves OCR only patchily.
- No crash frames in the run (`relay.log` empty: the GUI logged nothing beyond startup).
