---
id: RDQ7
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzrd
created: '2026-09-17'
acceptance: a normal start opens exactly one window; restoring a saved two-window layout opens exactly two
source: '`issues/bug_intake.txt`, 2026-09-17: "when relay opens, its opening a weird small second window."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A small second window opens when Relay starts

Since window restore landed, starting Relay opens an extra small window beside the real one. Likely
candidates: the saved layout holding a stale window, the restore path creating a window before the first
one is adopted, or the new window chrome creating a helper window that is mapped by mistake.

## Cause

None of the three. The extra window is a **`QLabel` that has no parent and is shown**, so Qt makes it a
top-level window of its own: `m_routeLabel`, the composer's routing-verdict label.

When the routing verdict was demoted to the mode chip's tooltip, the label stayed behind as the place its
text and tooltip live and was never added to a layout:

```cpp
// The routing verdict has no chip of its own: it is the mode chip's tooltip.
m_routeLabel = new QLabel;
m_routeLabel->hide();
```

`refreshProgramHint()` then ran `m_routeLabel->setVisible(text.isEmpty())` on every poll of the foreground
program. With no program running the text is empty, so the label was shown — as a parentless widget, that
is a new window. It is 8×19 px (an empty label's size hint), untitled, at 0,0, with no minimum size: the
"weird small second window".

Measured on a clean profile under Xvfb before the fix:

```
win=2097162 name='Relay — /tmp/tmp.VxgfCEVK5j' 1260x810
win=2097176 name='relay'                       8x19
mapped top-level windows: 2
```

Its X properties confirm an ordinary managed window (`_NET_WM_WINDOW_TYPE_NORMAL`, not a popup or a tool
window), and a debug dump of `QApplication::topLevelWidgets()` named it: `QLabel name='' visible=1 geom=8x19
parent=none`. The restore path and the window chrome were both innocent: the saved layout was read
correctly and `usableWindows()` returned exactly the windows it held.

## Implemented

- `src/main.cpp`, composer `buildUi()`: `m_routeLabel` is parented to the composer frame, so it can never
  become a window of its own even if something shows it later. The comment says why.
- `src/main.cpp`, `refreshProgramHint()`: dropped `m_routeLabel->setVisible(...)`. The label is a text and
  tooltip holder that feeds the mode chip's tooltip; it is never a widget on the row.
- `tests/windowstate_test.cpp`: `restoreOpensOneWindowPerSavedWindow` covers the restore decision, which is
  the pure half of the acceptance — a missing or empty layout restores nothing (so `main()` opens its one
  window), one saved window restores as one, and two restore as two with their own tabs and current index.

Build: `cmake --build build` with no new warnings. `ctest --test-dir build` 16/16; `./scripts/test.sh` 511
tests pass. One test is flaky on this machine —
`tests/test_subagents.py::HandoffTests::test_cancelled_main_turn_does_not_wake_and_keeps_result`, a
thread-timing race — and it fails at the same rate on an unmodified backend, so it is pre-existing and
unrelated.

## Evidence

`docs/qa_evidence/2026-09-17-bugfix-batch1/`, harness `windows.sh` (Xvfb; windows are captured by id,
because a root-window capture is black now that Relay draws its own frame):

- `clean-01-clean-start-win1.png`, `mapped top-level windows: 1` — a clean profile opens one window.
- `saved-01-first-run`, `saved-02-two-windows` (Ctrl+N), the saved `windows.json` holding two window
  records, and `saved-03-after-restore` — `mapped top-level windows: 2`, so a saved two-window layout
  reopens as exactly two.

## QA checklist

1. Start Relay with a clean profile (`XDG_CONFIG_HOME`/`XDG_DATA_HOME` in a temp dir): exactly one window
   appears. Check the window list, not just the screen — the stray window was 8×19 px at 0,0 and easy to
   miss behind the main one.
2. Open a second window (Ctrl+N), quit, start again without `--workspace`: exactly two windows reopen,
   where they were left.
3. Start with `--fresh` and with `--workspace PATH`: one window, and the saved layout is left alone.
4. Run a long command (`sleep 60`) and let it finish, then run `sudo true` and cancel it: the composer row
   must show the "… is running · prompts queue …" line while the program runs and nothing after it, and no
   extra window may appear at any point (this is the code path that used to show the stray label).
5. Take control (Ctrl+H) and give it back (Ctrl+Shift+H) a few times, and switch a pane to native input
   (F12) and back: still one window.
6. Hover the input-mode chip: its tooltip still ends with the routing verdict ("Where this line goes (Ctrl+I
   cycles). TERMINAL · …"), which is what the label now only holds text for.
7. On the owner's KDE desktop, start Relay normally and count the windows in the task switcher.
