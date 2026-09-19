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

## The walk, and what is behind the number (2026-09-19, owner: "fix that, and add CPU/MEM% to children")

Two things the pictures above could not have shown.

**The walk missed half the tree.** It read `/proc/<pid>/task/<pid>/children`, which is the *main
thread's* children. A process forked from any other thread is parented to that thread, and the
Python worker spawns its subprocesses off a worker thread — so the pane's biggest child could be
running unmeasured, and only turned up when the worker reaped it and its ticks appeared in
`cutime`. `relay::usage::walkTrees()` reads `/proc/<pid>/task/<tid>/children` for every thread now.
`tests/paneusage_test.cpp` (`childrenOfEveryThreadAreFound`) builds a `/proc` out of directories
with the child under thread 137 and nothing under the main thread, which is an arrangement no test
can ask the real kernel for; `relay::usage::setProcRoot()` is what points the walk at it.

**There is one walk.** `Pane::programWaitingForInput()` had its own, over the same shell pid on its
own 250 ms poll. It goes through `walkTrees()` now, asking for `Detail::PidsOnly` so that poll
still opens no `stat` or `statm`, and keeping its cap of 64; its decision logic and its cadence are
untouched. `oneWalkServesBothReaders` pins both forms to the same tree in the same order.

**The tooltips say what the number is made of.** Per-process CPU is that process's own tick delta
between two polls, keyed by pid *and* `starttime` — `perProcessDeltasKnowARecycledPid` feeds the
meter a pid the kernel handed out again and checks it is read as a new process and not as a
lifetime of ticks in one interval. The busiest five get a line each,
`<name> · X% cpu · Y% mem`, with the pane's two roots named `shell` and `agent worker`; rows that
round to 0 % on both axes are dropped, and a tab's tooltip merges its panes' rows and cuts them
back, so it names the tab's busiest processes and not each pane's. The chip and the tab label are
unchanged — still the sum alone, so nothing on screen grows or moves.

Not pictured: the breakdown under Xvfb. A tooltip needs a hover the smoke script has no way to
hold, and what it would show (`yes · 20% cpu · 0% mem`, five times) is what the unit tests assert
line for line. `relay-paneusage-tests` is 19 tests now.
