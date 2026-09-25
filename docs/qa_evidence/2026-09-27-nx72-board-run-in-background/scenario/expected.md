# Sealed expectations — #NX72 Try it

Opened by the verifying person only. The staged situation: card TRY1 ("Count to three") on a
disposable board, card page open, Run pressed twice (first press arms the "no plan yet"
confirmation, second hands the card over).

Expected results, each marked against the staged screenshots (01 card open, 03/04/05 at 0 /
300 / 1500 ms after handover):

1. At no moment does a terminal pane split the window beside the board. The window's layout in
   03, 04 and 05 is the card page; no right-half pane appears and disappears. (Before the fix,
   a pane opened at once beside the board, took focus, and closed again ~1 s later.)
2. `panes` reports exactly one foreground pane: the board. The agent pane is not in the
   foreground layout.
3. The card moves Inbox → Executing on its own thread: "owner claimed this card · assignee
   agent … session <8 chars>" and "Claimed (<8 chars>) · working on it from a terminal pane".
4. The Run button becomes "Running (<8 chars>)"; the click reveals the background pane
   (it is pulled into view — that reveal is deliberate, not the bug).
5. The pane is listed under Sessions → Background.

Run again with the pre-fix binary (git stash the RelayWindow.h change or an older build) and
1 fails: a pane flashes open beside the board and closes again within about a second.
