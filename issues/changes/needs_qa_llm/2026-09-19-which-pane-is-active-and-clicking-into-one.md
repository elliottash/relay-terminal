---
id: H3TQ
type: work
status: needs-qa-llm
labels: [change]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-19
rank: zzzzzzzj
created: '2026-09-19'
acceptance: a press anywhere in a pane makes that pane the active one and gives it the keyboard, and the active pane is told apart from its neighbours at a glance in every shipped theme
source: 'owner in chat, 2026-09-19: "its too hard to tell what is the active pane. and clicking into new panes doesnt make them active (it should)"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-active-pane-is-obvious/], related: [4PW5, 0T2R], github: null}
---
# Which pane is active, and clicking into one

## Issue

Owner, 2026-09-19: "its too hard to tell what is the active pane. and clicking into new panes
doesnt make them active (it should)".

## Cause

Two separate things, one for each half of the report.

**Clicking.** The active pane was decided by keyboard focus alone: `QApplication::focusChanged`
was the only path into `setActiveLeaf()`. So only the surfaces that take the keyboard moved the
active frame — the prompt box, a list, a text field. Everything a pane is mostly made of does not:
the terminal body is `Qt::NoFocus` under the prompt-box-only rules (`m_terminalFocusPolicy`, and
the FocusIn bounce that card #4PW5 describes), and so are the header, its title, the chips and the
pane's own margin. A click on any of those left the previous pane active and holding the keyboard,
so the next keystroke went to a pane the user had clicked away from. `implementer-before-02` is
that state: the click landed in the left pane, the bottom-right pane still has the caret.

**Telling them apart.** The active pane was marked by one hairline: `QWidget#pane` at
`1px solid @border` (`#2a2e37` in Relay Dark) against `1px solid @borderStrong` (`#4a5260`) when
active — 1.3:1 apart, on a `#0f1115` ground. The composer's accent border was doing the real work,
and it is at the very bottom of the pane.

## Implemented

- `src/RelayWindow.h`, `RelayWindow::activateOnPress()`: a `QEvent::MouseButtonPress` anywhere
  inside a leaf makes that leaf the active one. The press is never consumed, so every click still
  does exactly what it did. The keyboard is settled on the *next* event-loop turn rather than
  guessed at from focus policies: by then a widget that takes click focus has taken it, and if
  nothing in the new pane has the keyboard its prompt box is given it — which is what the frame
  now says is listening. A press that opened a popup (the model box, a menu) is left alone:
  focusing behind it would shut it (`implementer-after-06`).
- The shortcut hint the mouse path owes (`WARP.md`, standing rule) moved with it into
  `hintPaneFocusByMouse()`, so "Next time: Alt+Left / Alt+Right / Alt+Up / Alt+Down moves between
  panes" still appears on the first click-to-switch and not twice — the press now beats
  `focusChanged` to it, and `focusChanged` then sees no change.
- `src/Theme.cpp`: the active pane's own outline goes to `@muted` (`#8b919c` in Relay Dark, 5.6:1
  on the pane's ground instead of 1.3:1), and the pane's name in the header goes from `@muted`
  when it is not the active pane to `@text` when it is. `src/RelayWindow.h`, `repolishLeaf()`,
  hands `relayActive` to the title label the way it already did to the composer frame.
- `app/pane-theme.css` regenerated (`scripts/gen-web-theme.py`), so the browser and phone view of a
  pane carries the same cue. That file was **already** stale on `main` for an unrelated reason —
  the board-material tokens (`--rt-board-face`, `--rt-board-metal`, `--rt-board-metal-dim`) landed
  without it — so the regeneration brings those in too. It only adds; nothing in it is reverted,
  and `tests/test_web_theme.py` is green again.

Grey, not the accent, in both cues: the accent means "shell" in Relay's visual language — the
composer, the caret, the prompt chips — and `data/theme/themes/relay-dark.toml` says so where
`border_strong` is defined. A pane frame must not compete with it.

## Decisions

- **The path beside the pane's name stays legible in both states.** Dimming `QLabel#paneCwd` to
  `@disabled` was tried and dropped: on Relay Light that is `#a8aebb` on `#fbfbfd`, about 2:1 for
  9pt text, and legibility is a standing rule here (`docs/ARCHITECTURE.md`, "Legible text"). It is
  the pane's address, not a focus mark.
- **A tool pane is marked by its frame alone.** The Switchboard, the explorer, a diff or a preview
  has no prompt box and no `paneTitle` label — its header is a painted band whose label already
  carries a 4.5:1-on-fill contract (`relay::panestatus::TypeStyle`). Weakening that to show focus
  would trade an accessibility floor for a cue the frame already gives (`implementer-after-09`
  shows the board pane active). The frame is the one cue every pane type shares.

## Evidence

`docs/qa_evidence/2026-09-19-active-pane-is-obvious/`, `drive.sh [build-dir] [tag] [theme]` — three
terminal panes and a Switchboard under Xvfb with an isolated `HOME`, `XDG_*` and `TMPDIR`, clicking
into each pane's *terminal body* (the `Qt::NoFocus` surface the bug was about) and shooting the
window each time. `before` is this build's parent, `after` is the change.

- `implementer-before-02-clicked-left.png` — the click landed in the left pane; the bottom-right
  pane still holds the caret and the accented composer. The bug.
- `implementer-after-02-clicked-left.png` — the left pane is active: bright outline, its name at
  full strength, the caret in its prompt box, and the Alt+arrow hint shown once.
- `implementer-after-03/-04/-05/-10` — the same for each pane in turn, and back again.
- `implementer-after-06-model-box-of-an-inactive-pane.png` — a press on an inactive pane's model
  box: the pane becomes active *and* the dropdown is open (the black rectangle is the popup's own
  X window, which `import -window` cannot capture).
- `implementer-after-07/-08/-09` — a Switchboard tool pane and a terminal pane, clicked in turn.
- Measured rather than eyeballed: at the left pane's border column the pixel is `#8B919C` when
  active and `#2A2E37` when not, and the title's brightest pixel is 231/255 against 153/255.

Tests: `tests/themeswitch_test.cpp::theActivePaneIsVisiblyTheActiveOne` checks both rules against
the live tokens in all five shipped themes, and then the tokens themselves — the active outline
must differ from the resting one, clear 3:1 on the pane's ground (WCAG's floor for a non-text
mark), and be the louder of the two whichever way the theme runs. A theme whose `text_muted` sat
near its own background would otherwise spell the sheet correctly and put the cue back where the
complaint found it.

`ctest --test-dir build` 57/58; `./scripts/test.sh` 3304 tests, 2 failures. Both failures are
**pre-existing on a clean export of `HEAD`** and belong to other work:
`test_remote_wire.test_every_worker_event_is_classified` (`board_folder_changed` is not classified
in `remote/wire.py`) and, before this change regenerated it,
`test_web_theme.test_committed_file_is_current`.

## QA checklist

1. Three panes. Click once in the middle of each pane's terminal output — not its prompt box — and
   type: the text must go into the pane just clicked, every time. Then Ctrl+W: that pane closes.
2. The same click on the pane's header, on its title, on the `~/path` chip, on the pane's margin,
   and on the empty space beside the prompt box.
3. From across the window, with four panes open, say which pane is active before looking for the
   caret. Repeat in Relay Light, Dark Copper, Gruvbox Dark and IBM Beige (`/theme`), and with the
   window itself unfocused.
4. Click the model box of a pane that is not active: the dropdown must open *and* stay open, and
   that pane must become the active one. Same for the ⓘ button and the pane menu.
5. Drag a selection in an inactive pane's terminal output: the selection must work as before and
   the pane must become active. Then check the copy landed (copy-on-select, if it is on).
6. Open the Switchboard, an explorer and a diff pane. Click between them and a terminal pane: the
   frame must follow every time, and a card's keys (Enter, e, p) must reach the board after a click
   in it.
7. The hint "Next time: Alt+Left / … moves between panes" must appear on the first click that
   switches panes and then stop, and must not appear when Alt+arrow is used.
8. Drag a pane by its header onto another pane's edge: the drag must still start, and the dragged
   pane is the active one.
