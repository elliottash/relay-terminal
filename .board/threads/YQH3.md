<!-- relay:entry 20260925T223913Z-7w author=agent kind=event model=k3 pane=9c518869 turn=c1facb76ed574045a4ed7b5f45713f08/f8575b6367ac4042913be268fa647953 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-consolemode-flakes-on-different-cases-per-run-un.md

<!-- relay:entry 20260925T223918Z-qb author=agent kind=evidence model=k3 pane=9c518869 turn=c1facb76ed574045a4ed7b5f45713f08/f8575b6367ac4042913be268fa647953 -->
Measured 2026-09-25 while gating #6YS5 (which is unrelated — its verify gate was set to its own test for this reason):
- Run 1 (verify slot relay-terminal-1006c7a3-0): `FAIL ../src/tests/234z_cases.h:87 dead` — the two-press Esc program never died.
- Run 2 (same slot, seconds later): `FAIL consolemode_test.cpp:2004/2007` (▸ ran pytest / ▸ ran ctest transcript text) and `720/721/733` (edit-path checks).
- Same cases fail identically in the shared `build/` (tip + other sessions' uncommitted work): `2005/2008/721/722/734` there.
- `relay-consolemode-tests --234z-only` in the same slot build: **all cases passed**.
So the failing set varies run to run and machine load appears to be the variable; a clean-machine rerun would tell whether it still reproduces when idle.

<!-- relay:entry 20260925T223918Z-xk author=agent kind=event model=k3 pane=9c518869 turn=c1facb76ed574045a4ed7b5f45713f08/f8575b6367ac4042913be268fa647953 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
