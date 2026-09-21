# Copy on highlight needs three letters or digits (#C9VT)

Owner request, 2026-09-20: *"make it where, highlight to copy only works if there are at least 3
letters or numbers in the string"*.

## What was built

A highlight is copied only when it holds **at least three letters or digits**
(`QChar::isLetterOrNumber`, so Unicode letters count and punctuation, whitespace and symbols do
not: `a-b` is two, `a b c` is three). A shorter one is a slip of the mouse on the way to a click,
and it leaves the clipboard and PRIMARY exactly as they were — and says nothing, because nothing
was copied.

Four copy-on-highlight paths, all gated:

| Path | Where | What it writes |
| --- | --- | --- |
| the shared pane filter | `src/CopyOnSelect.h`, `CopyOnSelectFilter::eventFilter` | PRIMARY and the clipboard (and the pane's toast) |
| the terminal's own release | `engine/view/TerminalView.cpp`, `mouseReleaseEvent` | PRIMARY |
| the terminal pane's release | `src/Pane.h`, the app-wide event filter (`QTimer … copySelection()`) | the clipboard (and the toast) |
| the remote pane's screen | `src/RemotePane.cpp`, `RemoteScreen::mouseReleaseEvent` | PRIMARY, plus the clipboard with the setting on |

The plan named the first three; the fourth path — the *terminal pane's* clipboard half — was missed
by it and was found while executing: without it a two-character drag in a terminal pane would
still have taken the clipboard, which is the case the card is about.

`relay::copyOnSelectWorthCopying()` is the rule in `src/CopyOnSelect.h`; the engine cannot include
a header from `src/`, so `engine/view/TerminalView.cpp` carries the same three-line predicate in
its anonymous namespace, and both comments say the two must stay in step. **Explicit copies are
untouched**: Ctrl+Shift+C, Ctrl+C in the prompt box, the context menu's Copy and every copy button
still take a one- or two-character selection.

## Tests

    ctest --test-dir build -R copyonselect      # 1 test, passed
    ctest --test-dir build -R remotepane        # 1 test, passed

`tests/copyonselect_test.cpp` gained the rule's unit cases (empty, `ab`, `12`, `a-b`, `...`, `  a `
→ false; `abc`, `a1b`, `a b c`, `/home/elliott`, `ünï` → true) and a filter-level case: a
two-character selection on a read-only view is not copied and does not notify, while a
three-character one is. `remotepane` covers the `RemoteScreen` change.

## Live drive

    docs/qa_evidence/2026-09-21-copy-on-highlight-three-chars/drive.sh

A live Relay under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and
TMPDIR, `terminal/copy_on_select=true`, and a stub provider. The terminal is the measured case
because its highlight is two paths for one gesture (the engine's PRIMARY write and the pane's
clipboard write).

Where the marker line is comes from OCR of a screenshot's word boxes, not from a guessed
coordinate, and each candidate drag is measured by doing the same drag again and copying it
explicitly (**Ctrl+C with the prompt box empty**, whose behaviour the card leaves alone) and
reading the clipboard back — so the drive knows how many letters or digits the highlight holds
before it arms the sentinel and repeats the drag with copy on highlight alone.

### Result — 6 passed, 0 failed

The sweep at the marker line (press x=75, y=164) walked the covered text from one character to
five, so the two-character and three-character drags were measured, not assumed:

| Drag | Covered | Copy on highlight alone | Explicit copy |
| --- | --- | --- | --- |
| 8 px | `AA` | clipboard and PRIMARY both still the sentinel | `AA` |
| 18 px | `AAA` | clipboard `AAA`, PRIMARY `AAA` | `AAA` |
| 246 px | `AAAABBBBCCCCDDDDEEEEFFFFGGGG` | the whole line | — |

The raw pass/fail lines are in `implementer-clipboard.txt`, and
`implementer-0-grid.png` / `implementer-1-terminal-two-chars.png` /
`implementer-1-terminal-three-chars.png` / `implementer-1-terminal-wide.png` are the screenshots
the run took.

## Not covered here

A read-only pane surface (the conversation info pane, the Markdown preview, a card's body) is
covered by the filter's unit tests rather than by a live drag: those surfaces have no explicit-copy
route the drive can measure a short highlight with, so the live check above runs on the terminal.
The QA checklist carries a hand check on one of them.