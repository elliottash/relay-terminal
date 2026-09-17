---
id: ERES
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zz
created: '2026-09-17'
acceptance: Ctrl+Alt+arrow moves the focused pane in a split layout on the owner's KDE desktop, and dragging a pane by its grip moves it; both covered by a regression test or recorded evidence
source: '`issues/bug_intake.txt`, 2026-09-17: "ctrl + alt + right doesnt work to move the pane right. dragging panes doesnt work."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+Alt+arrow pane moves and pane dragging do not work

## Report

Owner, 2026-09-17, on a real KDE desktop with the Relay engine as the default: Ctrl+Alt+Right does not move
the pane right, and dragging a pane by its grip does nothing. Both were shown working under Xvfb when they
were implemented (`docs/qa_evidence/2026-09-17-pane-tab-buttons-moving/`).

## Checked so far

- Not a desktop shortcut clash: `~/.config/kglobalshortcutsrc` binds "Switch One Desktop to the Right" to
  Meta+Ctrl+Right, not Ctrl+Alt+Right, and "Switch to Next Desktop" is unbound.

## Suspects

- The prompt-box-only input change (`src/InputPolicy.*`, terminal widgets set to `Qt::NoFocus`, focus bounced
  back to the composer) may swallow or redirect the key and the mouse press on the grip.
- `PaneChrome` drag uses an event filter on the grip; the engine view's mouse handling changed in the same
  commit (it no longer grabs focus on press).
- Pane moves may need two panes in the same splitter with the right orientation; check the no-op paths in
  `runAction("pane.moveRight")`.

## Cause

None of the suspects. The key reaches the window filter and `runAction("pane.moveRight")` runs; the grip's
event filter gets its press and the drop zone is computed. Three separate defects were found, all
reproducible under Xvfb once the right step is taken:

1. **`pane.moveRight` and `pane.moveDown` were no-ops** (`RelayWindow::moveActive`). When the two panes are
   siblings in one splitter, the old code did

   ```cpp
   splitter->insertWidget(splitter->indexOf(neighbor), current);
   if (!towardStart) splitter->insertWidget(splitter->indexOf(current), neighbor);
   ```

   `QSplitter::insertWidget` **moves** a child the splitter already owns, and it numbers the target index as
   if that child had been taken out first. So the first call is already the whole swap in either direction,
   and the second call — only made for a move toward the end — put both panes back where they started. Left
   and Up worked, Right and Down did nothing and said nothing, which is exactly what was reported. The
   report says "Ctrl+Alt+Right"; Ctrl+Alt+Left on the owner's desktop would have worked.

2. **The pane chrome recursed into itself until the stack ran out** (`RelayWindow::showChromeFor`). It hid
   the old chrome *before* recording the new hover pane, and `QWidget::hide()`/`show()` make Qt deliver
   synthetic enter/leave and mouse-move events for whatever the change put under the cursor. Those come
   straight back into the window's application event filter, which calls `showChromeFor` again with the old
   state still in place. Clicking the ⇱ "Move to new tab" button **segfaulted Relay** every time (confirmed
   on unmodified `main`, backtrace is `showChromeFor → QWidget::setVisible → sendSyntheticEnterLeave →
   RelayWindow::eventFilter → showChromeFor …` repeated until SIGSEGV). Any pane move that hides a chrome
   while the mouse is over the affected area could hit the same path, which is the most likely reading of
   "dragging panes doesn't work" on a real desktop.

3. **A moved pane lost its screen on the Relay engine.** Moving a pane hides it, reparents it and shows it
   again inside one turn of the event loop, and Qt hands the view several sizes on the way. Instrumenting
   `TerminalView::applyGeometry` showed it being told `100x30` (→ 1 row × 10 columns) and `0x672`
   (→ 2 columns) while hidden; resizing the emulator to those reflows the screen and throws away what was
   on it. KonsolePart panes survive this, Relay-engine panes came back with a bare prompt — and the Relay
   engine is the process default the owner runs.

Not a cause, but worth recording: dropping a pane on the half of the neighbour that the two already share
is a no-op by design (the pane is asked to stay where it is). Dropping on the far half swaps them.

## Implemented (2026-09-17)

The layout decisions are now pure functions in `src/PaneLayout.{h,cpp}` (static library `relay-panes`,
ctest group `panes`, `tests/panelayout_test.cpp`, 16 cases), and `RelayWindow` is their only caller, so
there is one code path rather than a second one beside the old rules.

1. **`relay::panes::swapInSplitter`** does the sibling swap with the single `insertWidget` that Qt's
   semantics call for, keeping the splitter sizes. `moveActive` calls it for both directions;
   `pane.moveRight` and `pane.moveDown` now move the pane and repeat, and moving back and forth returns the
   original order. The non-sibling path (`takeLeaf` + `insertBeside`) is unchanged.
2. **`relay::panes::neighborIndex`** is the "which pane is on that side" rule that both Alt+arrow (focus)
   and Ctrl+Alt+arrow (move) use; `RelayWindow::neighborOf` now maps its leaves onto it.
   **`relay::panes::dropEdge`** is the nearest-edge rule `dropTarget` uses for a dropped pane.
   `moveActive`/`navigate` take a `relay::panes::Direction` instead of a Qt key code.
3. **`showChromeFor` records the wanted pane first and applies it in a bounded loop**, so a nested call
   from a synthetic enter/leave only notes what it wants instead of recursing. `takeLeaf` and
   `moveTabToNewWindow` clear the wanted pane alongside `m_hoverLeaf`. The ⇱ button no longer crashes.
4. **`TerminalView` applies its grid from a zero-timer** (`scheduleGeometry`, called from `resizeEvent` and
   the new `showEvent`) and ignores sizes while the view is hidden, so the emulator follows the size the
   view still has once the layout has settled. A moved pane keeps its screen and scrollback on both engines.

No shortcut, palette entry or hint changed: the keys, the ⠿ grip, the chrome buttons and the "Next time:
Ctrl+Alt+←…" drag hint are the same as before, they simply work now.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-pane-move-fix/drive.sh`, run once per engine under Xvfb + xdotool with the
prompt box holding the keyboard (`konsole-NN-*.png`, `relay-NN-*.png`), plus
`docs/qa_evidence/2026-09-17-pane-move-fix/drive-presets.sh` for the Konsole preset and the tab buttons
(`preset-konsole-NN-*.png`). Both `relay-stderr.log` files are empty.

- `NN-02`→`03`→`04`: two panes, the right one focused in its prompt box. Ctrl+Alt+Left swaps them and
  Ctrl+Alt+Right swaps them back — the reported failure, on both engines.
- `NN-05`/`06`: the same two moves while `sleep 60` runs in the focused pane.
- `NN-07`: Alt+Left still only moves the focus.
- `NN-08`/`09`: dragging the ⠿ grip shows the drop zone over the left half of the other pane, and the drop
  moves the pane there with its shell, its agent and its scrollback (`echo RIGHTPANE`, `sleep 60`, `^C`
  are all still on screen afterwards, on the Relay engine too).
- `NN-10`/`11`: dragging the grip onto the tab bar makes the pane a second tab.
- `NN-12`/`13`: the ⇱ "Move to new tab" button opens a third tab instead of crashing.
- `preset-konsole-01`–`03`: with `keybindings.json` on the Konsole preset, Ctrl+Shift+( splits and
  Ctrl+Shift+Left/Right move the focus between the panes.
- `preset-konsole-04`–`06`: the ⧉ button on a hovered tab moves that tab, with both its panes and their
  shells, into a new window.

Build: `cmake --build build` with no new warnings. `./scripts/test.sh` 382 passed;
`ctest --test-dir build` 12/12 (11 before, plus the new `panes` group). The new test was checked against
the old behaviour: reinstating the second `insertWidget` fails 4 of its cases.

The baseline for defect 2 was confirmed by building `main` unchanged into a separate tree and crashing it
the same way, so the segfault is not from this change.

## QA checklist

1. Split a pane (Ctrl+P), leave the keyboard in the prompt box, and press Ctrl+Alt+Right, then
   Ctrl+Alt+Left, several times each: the focused pane must move every time and end where it started. Do
   the same with Ctrl+Alt+Down/Up in a vertical split (Ctrl+Shift+P).
2. Repeat 1 while a program runs in the focused pane (`sleep 60`, then `vim`): the pane must still move,
   and the program must keep running.
3. Repeat 1 with three panes in one splitter: the middle pane must step past one neighbour at a time and
   the others must not change order.
4. With only one pane, Ctrl+Alt+Right must say "No pane in that direction." in the status bar.
5. Hover a pane, drag the ⠿ grip onto the far half of another pane: the drop zone must appear over that
   half and the pane must land there, keeping its scrollback, its running program and its conversation.
   Check the same on a pane in the other engine (palette › "New pane (Relay engine)", or start with
   `--engine=konsole`).
6. Drag the grip onto the tab bar: the pane becomes its own tab, again keeping its shell. Drag a pane from
   a second window onto this window's tab bar.
7. Press Esc during a drag: the drop zone disappears and nothing moves.
8. Click the ⇱ "Move to new tab" button on a pane in a tab that has two panes, with the mouse still over
   the pane afterwards: a new tab opens and Relay must not crash. Repeat a few times, and also click ×
   (close pane) and the two split buttons with the mouse resting over the chrome.
9. Alt+Left/Right/Up/Down must still only move the focus, never the pane.
10. Switch to the Konsole preset (Actions › keyboard shortcuts, or `keybindings.json` `"preset":
    "konsole"`): Ctrl+Shift+arrow moves the focus and Ctrl+Alt+arrow still moves the pane. Switch to the
    Warp preset: Ctrl+Alt+arrow moves the *focus* there and the move actions are unbound, which is the
    preset's own definition, not a bug.
11. Hover a tab and press its ⧉ button: the tab, its panes and their shells move to a new window.
12. On the owner's KDE desktop, repeat 1 and 5 with the Relay engine default, since that is where it was
    reported.
