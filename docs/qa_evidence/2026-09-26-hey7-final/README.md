# #HEY7 live restore check

Run `HEY7_LINES=50000 RELAY_BIN=<verified relay binary> bash docs/qa_evidence/2026-09-25-hey7-journal/stage.sh docs/qa_evidence/2026-09-26-hey7-final`.

The isolated pane printed 50,000 colored, bold numbered lines, quit, then reopened its saved layout. `files-3.txt` records 50,000 numbered lines across the sealed journal and newest tail, 50,000 distinct, min 1 and max 50,000. The journal held lines through 40,040; the tail began at 40,041. `03-restored-scrolled-top.png` shows the earlier-text link above the restored colored tail; `02-restored-top.png` shows its latest lines. The script's private profile was removed on exit.
