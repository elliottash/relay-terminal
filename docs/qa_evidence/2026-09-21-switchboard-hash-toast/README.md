# A hash copy in the Switchboard toasts (#Y2F4)

Owner request, 2026-09-20: *"when you copy to keyboard in the switchboard from clicking on a hash
code, send a notification \"#xxxx copied\" (like the highlight-text copy notification)"*. Asked which
of three things that meant, the owner chose **"Add the toast too"**: the copy keeps the board's own
"Copied #bug" notice line and *also* raises the copy-on-highlight-style toast, reading "#bug copied".

## What was checked

`drive.sh` is #3ZAP's drive with the toast checks added (and its two false positives fixed — see
below). It builds a two-card fixture board under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR`, `TMPDIR` and `RELAY_KEYRING=off`, opens the Switchboard, and clicks each copy
surface in turn. Every click is checked three ways: the notice line by OCR of the screenshot, the
new toast by OCR of the same screenshot, and the clipboard with `xclip`.

    docs/qa_evidence/2026-09-21-switchboard-hash-toast/drive.sh [build-dir] [out-dir]

## Result — 18 passed, 0 failed

| Step | Surface | Clipboard | Notice | Toast |
| --- | --- | --- | --- | --- |
| 02 | the list row's `bug` badge | `#bug` | "Copied #bug" | "#bug copied" |
| 04 | the card meta's `#bug` label | `#bug` | "Copied #bug" | "#bug copied" |
| 05 | `#bug` in the card body | `#bug` | "Copied #bug" | "#bug copied" |
| 06 | `#bug` in the thread | `#bug` | "Copied #bug" | "#bug copied" |
| 06b | 2.4 s later | — | still up (10 s) | **faded** |
| 07 | a `#KAN3` reference | untouched | — | — (it zoomed to that card) |

Screenshots and the raw pass/fail lines are beside this file (`ocr.txt`); `02-badge-click.png`,
`04-meta-click.png`, `05-body-click.png` and `06-thread-click.png` each show the toast at the pane's
bottom-right, `06b-toast-gone.png` shows it gone with the notice line still standing.

Unit test: `hashtagClicksCopyAndCardRefsZoom` (`tests/boardmodel_test.cpp`) pins the anchors, the
clipboard, the notice and the toast — its text, and its place at the pane's bottom-right rather than
the widget's top-left, which is where a popup placed before it is shown lands (the first cut of this
change did exactly that, and the drive caught it).

## Two false positives the drive had inherited from #3ZAP's

1. **The clipboard and notice checks were not conclusive.** Each step ran after the previous step's
   copy, and the notice line lives 10 s: a click that copied nothing still passed both. The drive
   now empties the clipboard before every copy click (`clip_clear`).
2. **The body step clicked the wrong word.** The body line reads "A bug #bug and a ref #KAN3", and
   the step looked for the first `bug` token on that line — the plain word, not the hashtag. With
   the clipboard cleared, that step failed: it had never clicked the hashtag at all. `hashtag_on_line`
   now takes the token the OCR saw *with* its `#`. The body hashtag does copy and does toast.

## Caveats

- Run on the shared checkout, whose `src/BoardPane.cpp` also carries another session's uncommitted
  work (the `Del` delete key hints, the `kCardSplitWidth` split floor, the check-strip verdict
  truncation). The landed hunks are this change's alone.
- `import -window root` takes about a second, and the toast is up for 1.6 s, so a shot can miss it:
  the drive clicks and shoots up to three times for the toast shots.
- The fixture's reference card id is pinned to `KAN3`: OCR read a random id (`#J883`) as `#/883`,
  and the reference step then had no position to click.
