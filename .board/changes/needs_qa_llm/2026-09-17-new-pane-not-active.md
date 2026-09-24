---
id: 4PW5
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzn1
created: '2026-09-17'
acceptance: a pane created by the split key or the pane button takes the keyboard at once — typing goes into its prompt box and Ctrl+W closes it
source: 'owner in chat, 2026-09-17: "after creating a pane, its not active yet, i cant type anything or ctrl + w to kill it."'
links: {plans: [], commits: [], evidence: [], related: [G152], github: null}
---
# A new pane is not active: typing and Ctrl+W do nothing

## Report

Owner, 2026-09-17, Relay engine as default: after making a pane it cannot be typed into and Ctrl+W does not
close it, so the window does not treat it as the active pane.

## Notes from a first look

`RelayWindow::split()` already calls `setActive(pane)` and, on the next event-loop turn, `pane->focusInput()`.
Suspects, in order:
1. `focusInput()` is a no-op for a pane that has not finished starting: a pane begins in native mode (the
   composer hidden) until its shell reports a prompt, so the call lands on the terminal, which now refuses
   focus under the prompt-box-only rules (`Qt::NoFocus`, the FocusIn bounce) — leaving no focused widget.
2. The window-level key filter resolves actions against the focused widget's pane, so with no focus Ctrl+W
   finds nothing.
3. The engine view's `showEvent`/zero-timer geometry work may steal or drop focus while the pane is created.

Fix so the new pane's prompt box has the keyboard as soon as it exists, whatever the shell has done yet, and
add a regression check (a pure helper, or an Xvfb step that types into a fresh pane).

## Cause

**Not reproduced.** Suspect 1 is not what the code does: a pane does not start in native mode. `m_native` is
false from construction, and `setNative(true)` only ever runs from the 5-second "shell integration did not
initialize" timer, the `unsupported` shell-hook reply, the alternate screen, a remote session or a password
prompt — none of which has happened when a pane is one second old. So `focusInput()` reaches
`m_editor->setFocus()`. Suspect 3 is not it either: `TerminalView::showEvent` only calls
`scheduleGeometry()`, and nothing in the engine view or its timers touches focus.

Eighteen Xvfb runs over the ways a pane gets made and the states the source pane can be in — split key,
split-down key, the pane chrome's "◫+" button, the palette entry, a split during the first pane's startup,
two splits in a row, a split while the source pane is in native mode, and a split while a program owns its
terminal — all typed into the new pane and all closed it with the close key, on **both** this branch and a
pristine build of `main` (`fa48101`) built into its own tree. Three panes behave the same. So the report is
real but its trigger is not in the paths that can be driven under Xvfb; the most likely difference is a real
window manager and window activation, which Xvfb has none of (X focus there is PointerRoot, so keys follow
the mouse whatever Qt's focus widget is).

Two real weaknesses in that path were found and fixed, either of which would produce exactly the reported
pair of symptoms if it were hit:

1. **The new pane's focus was deferred and never retried.** `split()` only queued `focusInput()` on a zero
   timer. Anything that takes focus while the pane is being inserted — and the insert shows the pane,
   reparents widgets and runs a splitter re-layout — wins, because nothing asks again.
2. **`focusTerminal()` could leave nothing focused at all.** In native mode it did
   `if (QWidget *target = m_backend->focusWidget()) target->setFocus();` and no more. A pane whose terminal
   is not up yet has no focus widget, so the call was a silent no-op: no widget in the window has the
   keyboard, nothing typed reaches the pane, and the window's shortcut filter has no pane under the focus.

## Implemented

- `src/main.cpp`, `RelayWindow::split()`: the new pane takes the keyboard synchronously, again on the next
  event-loop turn, and once more after 120 ms if it is still the active leaf — so a focus lost to the
  insert, the splitter re-layout or the engine view's geometry pass is taken back. `focusInput()` is
  idempotent. A `QPointer` guards the pane against being closed in between.
- `src/main.cpp`, `Pane::focusTerminal()`: with no terminal focus widget yet, the prompt box takes the
  keyboard instead of nothing. This is the "whatever the shell has done yet" the card asks for.

Build: `cmake --build build` with no new warnings. `ctest --test-dir build` 16/16; `./scripts/test.sh` 511
tests pass.

## Evidence

`docs/qa_evidence/2026-09-17-bugfix-batch1/`:

- `newpane.sh <engine>` — split with the key, type without clicking anything, press the close key, split
  again with the pane chrome button and repeat. The saved window layout is the pane counter.
  `newpane-relay-03-typed-into-the-new-pane.png` shows `echo FRESHPANE` in the **new** pane's prompt box;
  the count goes 1 → 2 → 1 for both the key and the button.
- `newpane-variants.sh <engine>` — the six states above, each reported PASS with the pane count before and
  after. `variant-relay-05-split-while-a-program-runs.png` shows the marker in the new pane while `sleep 60`
  runs in the old one.
- `newpane-three.sh <engine> <build> <tag>` — the same with three panes.
- The same harnesses against a pristine `main` build gave the same PASSes, which is how "not reproduced" was
  established rather than assumed.

## QA checklist

This one needs a human at a real desktop: it did not reproduce under Xvfb on either build.

1. On the owner's KDE desktop, with the Relay engine as the default, split a pane (Ctrl+P) and immediately
   type without touching the mouse: the text must appear in the **new** pane's prompt box. Then press Ctrl+W:
   that pane must close. Repeat ten times, including quickly in a row.
2. Repeat 1 with Ctrl+Shift+P (split down), with the pane chrome's "◫+" and "▤+" buttons, and from the
   palette (Ctrl+Shift+A › "Split right").
3. Repeat 1 with three and four panes open, and in a second window.
4. Repeat 1 while the source pane is in native mode (Ctrl+H first) and while a program runs in it
   (`sleep 60`, then `vim`).
5. Repeat 1 immediately after Relay starts, before the first pane's shell has settled, and in a pane whose
   shell integration failed (start with a `~/.bashrc` that already sets a DEBUG trap, or wait for the
   "Shell integration did not initialize" message): the new pane must still take the keyboard.
6. If it still happens, note what had focus at the time (was the palette open? had a dialog just closed? was
   the window freshly activated from another app?) and whether the pane's border shows it as active — that
   is the piece Xvfb cannot show.
