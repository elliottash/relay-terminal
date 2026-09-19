---
id: Q7Y9
type: work
status: needs-qa-llm
labels: [feature, gui]
implemented_by: claude
rank: zzzzzt
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-pane-beneath-chord/], related: [], github: null}
---
# Chord: Ctrl+Alt+Left/Right then Ctrl+Alt+Down docks the pane beneath that neighbor

## Issue
make it where i can push a pane beneath the one to the left by doing ctrl + alt + left + down, and vice versa for ctrl + alt + right + down to go beneath the one to your right.

## Notes
- The chord rides the #78BN pattern (`PlacementWindow`, two seconds), but its second key is the
  `pane.moveDown` **action**, not a bare arrow: no new action id, no new key, and it follows
  whatever the preset binds to the three move actions. Any other action, bare key or click closes
  the window without consuming anything; a plain Move-down on its own still moves the pane down.
- The pane keeps its shell, agent and scrollback: docking uses the same `takeLeaf` +
  `insertBeside` pair every other keyboard move uses.
- WARP.md's hint rule: dragging a pane onto another's bottom edge now hints the chord
  (`pane.dockBeneath`) instead of the generic `pane.drag` hint.
- Two-second toast over the moved pane teaches it live ("Ctrl+Alt+Down docks it beneath"),
  sharing the placement window's toast label.

## Tasks
- [x] Arm the two-second window after a left/right move; consume it in pane.moveDown (dock beneath the neighbor moved toward) <!-- t:r2 -->
- [x] Teach it: Keymap descriptions, palette subtitles, README, ARCHITECTURE, KEYBINDING-PRESETS, drag-to-dock hint, on-pane toast <!-- t:z2 -->
- [x] Build, ctest, and a live Xvfb drive with evidence under docs/qa_evidence/2026-09-19-pane-beneath-chord/ <!-- t:9k -->
- [ ] QA session by a non-Claude model over the checklist below <!-- t:2d -->


## Acceptance evidence
- Implementer evidence: `docs/qa_evidence/2026-09-19-pane-beneath-chord/` (drive.sh, notes.md,
  implementer-*.png/.txt/.log). Live under Xvfb: both chords dock beneath (geometry 36×71 →
  14×148 and OCR shows the moved label below the anchor), the window expires after two seconds,
  typed keys still reach the shell and close the window, the drag slow path hints the chord.
- `ctest` on a full build of the working tree: 53/54 passed; the one failure
  (`backend-and-bash`) is in another session's uncommitted backend edits (test_roles.py,
  test_sessions.py, remote/wire.py), none of which this feature touches. A full ctest re-run on
  the commit's scratch tree was skipped at the owner's request (2026-09-19); the commit adds
  docs, this card and evidence only — the code itself landed earlier in 68f9a41.

## QA checklist
- [ ] Two panes side by side, focus the right one: Ctrl+Alt+Left then Ctrl+Alt+Down (within 2 s)
      docks it beneath the left pane; the pane keeps its shell/scrollback (its `echo` output and
      cwd survive) and keeps focus.
- [ ] Focus the left one: Ctrl+Alt+Right then Ctrl+Alt+Down docks it beneath the right pane.
- [ ] Ctrl+Alt+Down on its own still moves a pane down when a pane is below, and after the
      two-second window a following Ctrl+Alt+Down is a plain move (no dock).
- [ ] Typing into the pane while the window is open still reaches the shell (nothing swallowed)
      and cancels the chord; so does a click, any other action, and closing/moving the pane.
- [ ] The toast "Ctrl+Alt+Down docks it beneath" appears over the moved pane for the window's
      two seconds and never outlives it.
- [ ] Dragging a pane by its ⠿ grip onto another pane's bottom edge still docks it beneath and
      shows "Next time: … then … · dock it beneath" (once, per the hint limits).
- [ ] Rebinding pane.moveDown in keybindings.json moves the chord with it (second key is the
      action, not the literal Ctrl+Alt+Down).
- [ ] No regression: #78BN's new-pane placement arrows, Alt+arrow focus, Ctrl+Alt+Up, pane
      swapping in a shared splitter, and cross-window drags all behave as before.
