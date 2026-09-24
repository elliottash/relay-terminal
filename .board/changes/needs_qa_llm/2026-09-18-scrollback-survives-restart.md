---
id: SB7K
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, agent C), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'Quit Relay with output on screen, start it again: the reopened pane shows that text above its new prompt, scrollable and in order; the store is bounded and pruned; `ctest` (29) and `./scripts/test.sh` (883) pass'
source: 'owner, bug intake 2026-09-18: "when relay quits and restarts and reloads sessions, it should still have the scrollback"'
links: {plans: [], commits: [95f05b4], evidence: ['docs/qa_evidence/2026-09-18-scrollback-survives-restart/'], related: [], github: null}
---
# Scrollback survives a quit and restart

## Report

"Reopen where I left off" brings back windows, tabs, splits, directories, models and each pane's
agent conversation — and an empty terminal. Everything the user had read in that pane was gone the
moment Relay quit, so the restored pane looked like a fresh one with somebody else's history
attached.

Root cause: the saved layout (`$XDG_DATA_HOME/relay/state/windows.json`) only ever described how
to *rebuild* a pane. Nothing wrote the terminal's own text anywhere, and the restored pane starts a
brand-new shell in a brand-new emulator, whose scrollback begins empty. The architecture doc said
so in as many words: "Terminal scrollback is never restored — the shell is new."

## Change

Each pane node now carries a `scrollback` id, and the text behind it lives beside the layout, one
file per pane: `$XDG_DATA_HOME/relay/state/scrollback/<id>.txt`, mode 0600, written through
`QSaveFile` exactly as `windows.json` is. The id is the pane's token, and a restored pane keeps the
id it was saved with, so a pane rewrites its own file instead of leaving one behind per restart.

- **What is saved: text, not cells.** `TerminalBackend::scrollbackText()` plus the visible screen
  (skipped while a full-screen program owns it, whose frame is not output worth keeping), without
  colour. Two reasons. The engine's introspection hands the host text; cells and their SGR would
  need a new `VtCore` call implemented in both cores, which is far more than the loss is worth.
  And absolute colour does not survive a replay: `38;2;R;G;B` written into a new pane is burnt into
  its history, because a terminal cannot recolour its scrollback (`src/MarkdownAnsi.h`), so text
  saved under one theme would come back in the old theme's colours for good — the same trap that
  made near-white prose unreadable on IBM Beige. Restored lines take the live theme's foreground
  instead. If styling is ever wanted here, the thing to save is *indexed* SGR, which the engine
  resolves at paint time, never RGB.
- **Where, and how it is bounded.** The same directory as the layout, so there is one state
  location, not two. Per pane: at most 5,000 lines and 512 KiB (`kScrollbackMaxLines`,
  `kScrollbackMaxBytes` in `src/WindowState.h`), newest kept, trailing blank rows dropped — both
  caps well inside the engine's own 20,000-line scrollback, so nothing saved is more than a pane
  could hold. A pane with no text leaves no file; every layout write prunes the files of panes the
  layout no longer names; "Start a fresh window set", `windows.fresh` and turning
  "Reopen windows on start" off delete the whole store along with the layout.
- **When it is written.** Only on the way out: `WindowManager::saveScrollbacks()` from
  `noteWindowClosing()` (a quit closes every window, and the snapshot is taken while all their
  panes are alive) and from `aboutToQuit`. Never on the 1 s layout debounce, which fires on every
  split, drag, resize and title change and must not read thousands of lines per pane. The cost is
  that a crash loses the scrollback, where it loses only a second of layout.
- **How it comes back.** At the restarted shell's *first prompt* — not at pane construction, where
  the grid is still 24×80 and the text would wrap at the wrong width — the pane erases the prompt
  line the way inline agent output does, prints the saved lines plain between two muted rules
  ("— scrollback from before the restart —" / "— end of restored scrollback; this shell is new —")
  and sends an empty line so the shell prints a fresh prompt underneath. `redrawPrompt()` is *not*
  enough here: Readline still believes its prompt is where it drew it and the restored block has
  just scrolled the screen out from under it, so the repaint is a no-op and the pane is left with
  no prompt at all (the trap `clearTerminal()` already documents). The rules are Relay's own
  chrome and are filtered back out when the pane saves again, so they do not stack up over
  restarts. Replayed lines go through `sanitize()`, so a hand-edited or truncated file cannot
  drive the terminal, and ids are validated (`isScrollbackId`) so a layout cannot point the store
  at `../`.
- **Nothing is re-run.** The restored text is text. The shell is new, the agent session is
  reattached as before, and the rule under the block says so.

Files: `src/WindowState.{h,cpp}` (the store: `scrollbackDirectory`, `isScrollbackId`,
`scrollbackPath`, `clampScrollback`, `writeScrollback`, `readScrollback`, `scrollbackIds`,
`pruneScrollback`, `removeAllScrollback`), `src/main.cpp` (`Pane::saveScrollback()`,
`Pane::replayRestoredScrollback()`, the `scrollback` key in `serializeNode`/`initRestore`,
`RelayWindow::savePaneScrollbacks()`, `WindowManager::saveScrollbacks()` and the prune in
`writeWindows`), `tests/windowstate_test.cpp` (6 new cases), `docs/ARCHITECTURE.md` (section 3),
`docs/VALIDATION.md`. `docs/AGENT-SESSIONS-PROTOCOL.md` is unchanged: no GUI ↔ worker message
gained or changed, because the terminal's text never goes near the worker.

## QA checklist

1. **The report.** With Relay running, print something long in a pane (`seq 1 200`), quit Relay
   (close the last window), start it again. The reopened pane shows that output above its new
   prompt, oldest line first, with a muted rule above and below it, and a working prompt under the
   rule. Ctrl+Shift+W is not involved.
2. **Scrollable and in order.** PageUp walks up through the restored text to the top rule; the
   lines are in the order they were printed, nothing is reversed or interleaved. PageDown returns
   to the live prompt.
3. **Splits and tabs.** Two panes side by side with different output, plus a second tab: after a
   restart each pane has its own text, not the other's.
4. **More than one window.** Two windows open, quit: both come back with their own scrollback.
   Closing *one* window of two and quitting later must not resurrect the closed window.
5. **The shell is new.** `echo $$` before and after differs; no command is re-run; a command left
   half typed in the prompt box is not restored (it never was).
6. **Bounded.** Print far more than the cap (`seq 1 200000`), quit, check
   `~/.local/share/relay/state/scrollback/*.txt`: no file above ~512 KiB or 5,000 lines, and the
   restored pane shows the *newest* lines. Nothing in the directory belongs to a pane that no
   longer exists in `windows.json`.
7. **Privacy.** The files are 0600 in a 0700 directory. "Start a fresh window set" (Actions) and
   turning off "Reopen windows on start" both delete `state/scrollback/` as well as
   `windows.json`. `relay --fresh` keeps both, as it keeps the layout.
8. **Full-screen programs.** Leave `vim` or `htop` running in a pane and quit: the restored pane
   does not replay the program's frame, only the scrollback behind it. (Relay does not re-run the
   program.)
9. **Restart twice.** Quit and restart a second time: exactly one pair of rules is shown, not one
   pair per restart, and the output typed after the first restore is saved with the rest.
10. **Nothing regressed.** The layout itself still restores (geometry, screen, tabs, current tab,
    directories, models, session ids). A second Relay still refuses to save. `ctest --test-dir
    build` (29 groups) and `./scripts/test.sh` (883) pass — both re-run green on main after the
    code landed in 95f05b4.

## Known gaps

- **No colour.** Restored output is plain text: `ls` colours, `git diff` red and green and agent
  prose all come back in the theme's default foreground. This is the deliberate trade above; the
  alternative needs a cell-level engine API *and* indexed-only colour to be safe across themes.
- **A crash loses it.** Only a real quit (or closing a window) writes the text. Writing it on the
  1 s debounce would mean reading every pane's scrollback every time a splitter moves.
- **Closing a pane is still final.** Ctrl+Shift+W reopens a closed pane with the same node — and
  therefore the same `scrollback` id — but nothing writes the text when a single pane or tab
  closes. It comes back empty, unless an earlier quit had saved that pane, in which case it
  replays what that quit saved rather than what was on screen when the pane was closed.
- **The old prompt line is part of the text.** The saved screen ends with the prompt the pane was
  sitting at, so the restored block shows that prompt line above the closing rule. It is real
  output, so it is kept rather than guessed at.
- **Width.** Lines are replayed at the width the pane has when the shell's first prompt appears.
  A pane restored much narrower than it was saved wraps the old text at the new width, like any
  terminal output printed at that size.
