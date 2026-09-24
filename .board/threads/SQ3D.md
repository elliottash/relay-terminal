<!-- relay:entry 20260921T203857Z-a1 author=agent kind=progress model=claude-opus-5 -->
Claimed by Claude Code (land session `slq3-ink`). Inking the leading `/command` of an echoed prompt with an indexed SGR colour in `Pane::printInline`, and adding `legibleOn()` so the view keeps a written ink readable on the role row's band.

<!-- relay:entry 20260921T210217Z-b2 author=agent kind=evidence model=claude-opus-5 -->
Landed 27a6134 through land.py (build gate: the exact committed tree builds `relay`). FaintInkTest 24 passed; ViewTest::aCommandInAUserRowKeepsItsHueAndClearsItsBand passed and was checked to fail with either view branch disabled. GUI: docs/qa_evidence/2026-09-21-slash-command-ink/ — isolated Xvfb, stub provider on loopback, `/deliver trace and fix the bug…` echoed on Relay Dark (light band) and IBM Beige (dark band) with nothing retyped; analyse.py reads the painted ink back as exactly legibleOn(palette[14], band) at 4.51:1 in both, the raw palette colour (1.63:1, 1.22:1) nowhere on the row.

<!-- relay:entry 20260921T210217Z-b3 author=agent kind=note model=claude-opus-5 -->
This card was filed as #SLQ3, which is not a valid id — `L` is not in the Crockford alphabet the board uses — so the card file, the thread and every `#SLQ3` in the code comments and evidence were renamed to #SQ3D. `issues/BOARD.md` is left as it is: regenerating it here would have carried rows for 19 other sessions' uncommitted cards onto main.
