# Pane CPU / memory meters — implementer evidence (card #D03W)

Run of the real app under Xvfb (`implementer-smoke.py`, 2026-09-19): one fresh pane, the
instruction dialog dismissed, the keyboard handed to the terminal with Ctrl+H, then

    for i in 1 2 3 4; do yes > /dev/null & done; sleep 300

typed into the shell. Four busy processes on a 20-core machine: the pane is using 20 % of the
machine's CPU and ~0 % of memory, sampled over the 400 ms status poll.

- `implementer-pane-chip.png` — the whole window. Tab label ends `· 20% / 0%`; the pane header
  shows the chip `20% 0%` right of the "Command running" state word.
- `implementer-header-zoom.png` — the same, tab bar and pane header cropped and doubled.

What is not pictured: the `cpu 12% · mem 3%` tag on a conversation's row in the Sessions pane.
Driving it needs a configured agent (a live session id), which the keyless smoke does not have;
`tests/conversations_test.cpp` (`liveUsageTagsComeAndGo`) covers the tag appearing, changing and
leaving on a real `SessionManager`, and `tests/paneusage_test.cpp` covers the arithmetic and the
`/proc` walk (12 + 31 tests).
## Correction (2026-09-19, GUI review)

`implementer-header-zoom.png` and `implementer-pane-chip.png` are kept as the record of a bug,
not of the shipped look: their tab label reads **`empty-ws Â· 20% / 0%`**. The separator had been
written as the escaped UTF-8 bytes `"\xc2\xb7"` inside a `QStringLiteral`, which builds a UTF-16
literal out of whatever bytes it is handed, so the two characters `Â·` reached the tab. The text
of NOTES.md above describes the intended `·` and not what those two pictures show.

`review-tab-suffix-2026-09-19.png` is the same reading on today's build (three panes, six `yes`
processes in the focused one, isolated `HOME`/`XDG_*`/`TMPDIR` under Xvfb): the tab reads
`project · 3 · 30% cpu` with one `·` and the memory half left out, because the pane's resident set
is under the 256 MiB floor. The same run also showed that the chip appearing and leaving moves no
splitter: the three panes' dividers are at the same x in the idle, busy and subagent frames.
