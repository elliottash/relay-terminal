---
id: GWXM
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: 4n
created: '2026-09-17'
acceptance: after `ls`, `grep -n` or a compiler error, Ctrl+Shift+L highlights a path or URL in the pane and repeated presses / arrows move between them, scrolling scrollback as needed; Enter opens it with the same routing as a click (explorer, preview, browser); Esc cancels
source: '`issues/feature_intake.txt`, "add a shortcut to scroll through files / folders / links in the output, maybe alt page up / alt page down."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-clickable-paths/'], related: ['issues/features/needs_qa_llm/2026-09-17-clickable-paths.md'], github: null}
---
# Keyboard shortcut to step through files, folders and links in output

## Dependency (resolved)

The walk needs the pane's screen and scrollback text plus a way to highlight a range.
KonsolePart 23.08 exposes neither, so this is implemented on **Relay's own engine**, which is now
the process default (`docs/ENGINE.md`, `src/TerminalBackends.cpp`). A KonsolePart pane says so and
does nothing.

## The key: Ctrl+Shift+L

Checked against `docs/KEYBINDING-PRESETS.md` and `Keymap::presetJson()`: Ctrl+Shift+L is unbound in
the Relay defaults and in all four presets (`relay`, `warp`, `vscode`, `konsole`), so it is left out
of the preset tables and every preset inherits it. It is a Ctrl+Shift combination, so it also acts
while a program owns the terminal under the default `program_keys: "shift-only"`.

Alt+PageUp / Alt+PageDown from the intake note was not used: Alt+arrows are Relay's pane focus keys
and PageUp/PageDown are the composer's scrollback keys, and a single key that starts a mode reads
better than two.

## Behavior as implemented (2026-09-17)

- New keymap action **`links.step`** — "Step through files, folders and links in the output
  (Enter opens, Esc leaves)", category `terminal`, default **Ctrl+Shift+L**. It also appears in the
  actions palette as "Step through links in the output" (engine panes only, gated on the new
  `TerminalBackend::LinkWalk` capability) and in the shortcuts overlay.
- **It works while the prompt box has focus**, which is the normal state: the action goes through
  `RelayWindow::eventFilter` like every other Relay shortcut, and while a walk is running that
  same filter takes Enter, Esc and the four arrows before the composer sees them.
- The first press highlights the **newest** link (the one nearest the prompt) and shows
  `N of M · <absolute target>[:line] · Enter opens, Esc leaves` in the status bar.
- **Ctrl+Shift+L again, Up or Left** step towards older output; **Down or Right** step towards
  newer. Both ends wrap. A link above the viewport is scrolled into view (a third of the way down).
- **Enter** opens it with exactly the routing a click uses (`Pane::openOutputTarget`): a folder in
  an explorer pane, a file in a preview pane at its line, a URL in the browser.
- **Esc** leaves the walk. So does clicking in the terminal, and so does typing any character into
  the prompt box — the key is then handled normally, so nothing is swallowed.
- The highlight is the core's selection plus the hover underline, so it survives repaints and
  scrolling; ending the walk clears both.
- The list is built once when the walk starts and kept until it ends, so repeated presses move
  through the same links even while the program keeps printing.

**Where the list comes from** (`TerminalView::collectLinks`): the newest 2 000 scrollback lines plus
the active screen, rows that filled the last column joined with the next one (a wrapped path is one
link again), each logical line run through `relay::links::scan` (`src/OutputLinks.*` — the same
rules as clicking, see the sibling card YZTK), capped at 500 links. Positions are absolute rows, the
same coordinates as `scrollViewportToRow`. The cursor itself is `relay::links::Cursor`, unit-tested
on its own.

New `TerminalBackend` API (`engine/TerminalBackend.h`): capability `LinkWalk`, struct `Link`, and
`stepLink(delta, Link*, index*, count*)`, `endLinkWalk()`, `linkWalkActive()`,
`setPlainClickOpensLinks(on)`. All non-pure, so `KonsoleBackend` inherits the "cannot" defaults.

## Explicit gaps

1. **KonsolePart panes cannot do it.** Ctrl+Shift+L there shows "This pane's engine cannot read the
   screen; start a pane with the Relay engine to step through links." (verified live).
2. **Only 2 000 scrollback lines and 500 links.** A longer build log is walked from its newest
   2 000 lines only; there is no "keep going" past the cap.
3. **Wide characters can shift the highlight.** The text index → cell mapping divides by the column
   count, so CJK or emoji earlier on the same line can put the underline a cell or two off. The
   target itself is always right.
4. **The list is not refreshed while a walk runs.** New output during a walk does not add links
   (leave and press again). If the scrollback cap evicts lines mid-walk, a step can land near
   rather than on the link.
5. **No hint that the walk is running other than the status line and the highlight** — no overlay,
   no count chip.
6. **No "open in the system editor" from the keyboard** (that is a context-menu entry only), and no
   way to copy the highlighted path without opening it.
7. **Alternate screen**: the walk still reads the alternate screen's text when a full-screen program
   is running, which is rarely useful. It was not special-cased.

## Implementer check (not a QA verdict)

Build clean, `./scripts/test.sh` 508 OK, `ctest --test-dir build` 17/17.
Tests: `tests/outputlinks_test.cpp` (the cursor: first step lands on the newest either way, wraps at
both ends, survives a shrinking list, cancels) and
`engine/tests/ViewTest.cpp::keyboardLinkWalk` (three links, one of them pushed into the scrollback:
newest first, two steps back into history, the selection is non-empty, wrapping, `endLinkWalk`).

Live, under Xvfb, with the **prompt box focused**
(`docs/qa_evidence/2026-09-17-clickable-paths/`): Ctrl+Shift+L showed
`15 of 15 · /tmp/…/my file.txt · Enter opens, Esc leaves` and highlighted the quoted name with the
space across two wrapped rows; three presses of Up reached `12 of 15 · /tmp/…/src/main.cpp:3` with
`src/main.cpp:3:24` highlighted; Enter opened `main.cpp` in the preview pane. On `--engine=konsole`
the status line reported the engine limit and nothing else happened.

## QA checklist

1. In a default pane, run `ls`, then `grep -n <word> <file>`, then something that prints a compiler
   error. With the **prompt box focused** (do not press F12), press Ctrl+Shift+L: the newest link is
   highlighted and the status bar shows `N of M · <path> · Enter opens, Esc leaves`.
2. Press Ctrl+Shift+L repeatedly, then Up/Left and Down/Right: the highlight moves one link at a
   time in the expected direction, wraps at both ends, and the status count follows.
3. Step far enough back that a link is above the viewport: the pane scrolls to show it.
4. Enter on a file link → the preview pane opens at the right line. Repeat for a folder (explorer)
   and for a URL (`echo https://example.com`, opens the browser).
5. Esc leaves the walk: the highlight and the underline go, and the next Enter in the prompt box
   submits normally. Do the same with a click in the terminal, and by typing a letter.
6. Start a walk, then let the shell print more output: the walk keeps working on the links it had.
   Leave and press again to pick up the new ones.
7. While `vim`/`htop` is running (alternate screen, composer hidden), Ctrl+Shift+L should not break
   anything (see gap 7).
8. `relay --engine=konsole`: Ctrl+Shift+L reports the engine limit and does nothing.
9. Shortcuts: Ctrl+? / F1 lists "Step through files, folders and links in the output"; the palette
   (Ctrl+Shift+A) has "Step through links in the output" with the key next to it; the entry is
   absent for a Konsole pane. Rebind `links.step` in `keybindings.json`, reload, and confirm the new
   key works and the palette detail text follows.
10. Each of the four presets (`relay`, `warp`, `vscode`, `konsole`): Ctrl+Shift+L is free and does
    the walk; nothing else in that preset stopped working.
11. Click a path with the mouse a few times and confirm the shortcut hint "Next time: Ctrl+Shift+L …"
    appears (at most 3 times, per the hint registry).
12. Nothing regressed: `./scripts/test.sh`, `ctest --test-dir build`.
