# #Q7Y9 — "move left/right, then ↓ docks it beneath" (implementer evidence)

Feature: a `pane.moveLeft` or `pane.moveRight` followed by `pane.moveDown` within two seconds
docks the focused pane beneath the neighbor it moved toward (default keys Ctrl+Alt+Left then
Ctrl+Alt+Down, and Ctrl+Alt+Right then Ctrl+Alt+Down). Implemented in `src/RelayWindow.h`
(`armBeneathDock` / `endBeneathDock` / `dockBeneathNeighbor`, arming at the end of `moveActive`,
consumption first in `pane.moveDown`), reusing `PlacementWindow`'s two-second window and the
shared on-pane toast; descriptions in `src/Keymap.h`, palette subtitles, README, and
`docs/ARCHITECTURE.md` / `docs/KEYBINDING-PRESETS.md` updated. Dragging a pane onto another's
bottom edge now hints the chord (`pane.dockBeneath`).

Run: `RELAY_QA_DISPLAY=:94 docs/qa_evidence/2026-09-19-pane-beneath-chord/drive.sh` (Xvfb +
xdotool + ImageMagick, isolated `XDG_CONFIG_HOME`, 1400×900). The focused pane reports its own
geometry (`stty size`, rows then columns) before and after each chord.

## Results

- `implementer-chord-left-before.txt` `36 71` → `implementer-chord-left-after.txt` `14 148`:
  the focused RIGHTPANE went from a tall half-width side pane to a wide short pane.
- `implementer-chord-right-before.txt` `36 71` → `implementer-chord-right-after.txt` `14 148`:
  same from the other side, focused LEFTPANE.
- Splitter orientation, machine-checked from the PNGs (unique colors down the center column: a
  uniform vertical splitter reads ~6–7, a stacked layout with text reads 31):
  - `02-two-panes` side-by-side; `04` and `05` stacked (both chords docked);
  - `06` (window expired after 3 s): Ctrl+Alt+Down was a plain Move-down — layout still
    side-by-side, no dock;
  - `08` (letter + Return typed inside the window): the keys reached the shell (OCR:
    `TYPEDWHILEOPEN` echoed) and closed the window — Ctrl+Alt+Down did not dock;
  - `10` (drag onto the bottom edge): stacked beneath.
- OCR of the screenshots: `03` shows the toast "Ctrl+Alt+Down docks it beneath"; `04` reads
  LEFTPANE above RIGHTPANE; `05` RIGHTPANE above LEFTPANE; `10` LEFTPANE above RIGHTPANE plus
  the hint "Next time: Ctrl+Alt+Right then Ctrl+Alt+Down · dock it beneath".

**Correction (2026-09-19, #Q7Y9 review).** The hint in screenshot `10` names the wrong key, and
the screenshot is kept as the record of that bug. `drive.sh` drags the RIGHT pane (grip at
`width - 6`) onto the LEFT pane's bottom edge, so the pane has to travel *left* to reach its
anchor: the chord that does the same thing is **Ctrl+Alt+Left then Ctrl+Alt+Down**. The hint
named the side the dragged pane came from instead, which is the move away from the anchor. The
follow-up commit swaps the two (`relay::panes::moveToward`, covered by
`tests/panelayout_test.cpp::chordNamesTheMoveTowardTheAnchor`). The same commit also stops the
arming text in screenshot `03` from being a raw toast on every left/right move: it goes through
the shortcut-hint registry now (id `pane.dockBeneath.chord`), so it appears a few times and then
stops, the "Shortcut hints" setting turns it off, and it names the two seconds rather than "now",
because a queued hint can reach the screen after the window has closed.

Re-driven live on the same Xvfb rig for the follow-up (same build, isolated `XDG_*` and
`TMPDIR`, screenshots kept out of the repo):

- the drag of the right pane onto the left pane's bottom edge now reads
  "Next time: Ctrl+Alt+Left then Ctrl+Alt+Down · dock it beneath" (OCR);
- the arming line reads "Ctrl+Alt+Down within two seconds docks it beneath", and it is skipped
  when the registry's global gap is still running — which is the point of routing it there;
- Ctrl+Alt+Left, then **Ctrl+C**, then Ctrl+Alt+Down: the panes stay side by side (6 unique
  colours down the centre column, against 30 for a stacked layout), so the shell key closed the
  chord's window instead of leaving it armed;
- the chord itself still docks (36×71 → 14×148, 30 unique colours down the centre column).

`ctest` on a full build of the working tree: 53/54 passed; the `backend-and-bash` failures are in
another session's uncommitted backend edits (`test_roles.py`, `test_sessions.py`,
`remote/wire.py`) and none of this feature's files are among them. A full ctest re-run on the
commit's own scratch tree was skipped at the owner's request (2026-09-19). Where the code lives:
another session's commit sweep first carried it to the `feature/guest-agents` branch (68f9a41);
it lands on main in the #Q7Y9 follow-up commit, built as main's own content plus these hunks.
