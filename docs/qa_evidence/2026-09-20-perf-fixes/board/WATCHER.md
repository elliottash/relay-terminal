# #N5JJ — what the Switchboard's watcher notices (before / after)

Measured on **spark**, 2026-09-20, Qt 5.15.13, offscreen.

The pane holds a `QFileSystemWatcher` on the board's **directories**. A directory watch fires when
an entry is created, renamed or removed — never when an existing file's content changes. The
profile found about 21 of 60 writes reaching the pane in a one-write-a-second storm.

## Harness

[`storm_watch.cpp`](storm_watch.cpp) builds the real `relay::BoardView`, hands it a `board` event
naming one card as `in-progress` — what a card an agent is executing looks like — shows it, and
counts the `board_refresh` messages the pane sends over 60 writes at one a second, cycling the card
file, its thread and `BOARD.md` exactly as the profiler's `storm.sh` did.

* `inplace` appends with `O_APPEND`: a guest CLI, an editor, a shell `>>`.
* `replace` writes a temporary file and renames it: what every writer Relay owns does.
* `watch` writes nothing and lets [`storm_relay.py`](storm_relay.py) do it through
  `relay_core.board`'s own writers (`Board.save`, `append_thread`, the index), from the matching
  tree.

```
g++ -O2 -fPIC -std=c++17 $(pkg-config --cflags Qt5Widgets) -I<tree>/src -o storm storm_watch.cpp \
    <build>/librelay-board.a <build>/librelay-helperchat.a <build>/librelay-editor.a \
    <build>/librelay-prompthistory.a <build>/librelay-toollabel.a <build>/librelay-voice.a \
    <build>/librelay-projects.a -lQt5Widgets -lQt5Gui -lQt5Core
QT_QPA_PLATFORM=offscreen ./storm <workspace> <card id> 60 inplace|replace|watch
```

**before** = the clean export of `main` the #PF4K profilers used; **after** = this tree. Each run
gets its own copy of the repo's board (353 cards).

## Results, 60 writes at one a second

| Who is writing | before | after |
|---|---|---|
| **Relay's own writers** (`Board.save`, `append_thread`, the index) | **40 of 60** | **60 of 60** |
| A foreign writer appending **in place** to the card the pane is working on | **0 of 60** | **40 of 60** |
| A foreign writer that **replaces** its files | 60 of 60 | 60 of 60 |

Row 1 is the fault the card is about. Exactly one of Relay's three writers appended in place —
`Board.append_thread`, and `carry_thread` with it — so every thread write (an agent's progress
note, a comment, every `✦ …` event line) was one of the 20 the pane never saw. Both write a
temporary file and rename it in now, which is what the card saves and the index already did.

Row 2 is the other half. A guest CLI or an editor writing in place is still invisible to a
directory watch, so the pane now keeps a file watch on the card the page is open on, on its
thread, and on the cards in `in-progress` while the 32-file budget lasts (of a 200-watch
allowance, 11 of which are this board's folders). The 20 writes still missed there are the
`BOARD.md` touches — a generated index, not a card, and nothing on the board draws from it.

Anything neither of those catches is picked up when the pane is looked at again: one debounced
`board_refresh` on show and on focus-in. No timer — #057J is removing idle wakeups — and with the
parse cache from #7M6E a refresh that finds nothing costs about 6 ms of worker at 353 cards.

## Tests

* `tests/test_board.py::ThreadTests` — an append gives the thread file a **new inode**, so a
  directory watch sees it; and four threads appending 6 entries each all keep their entry, which
  is what the move of the lock from the file to the threads directory is for (`os.replace` gives
  the path a new inode, so a lock on the old one stops excluding anybody).
* `tests/boardwatch_test.cpp` (new target `boardwatch`) — an in-place append to a worked card's
  thread reaches the pane, and showing the pane asks for a refresh. Negative control: with the
  same card in `inbox` (so it gets no file watch of its own) the first test fails on the timeout,
  which is the bug this card reports.
