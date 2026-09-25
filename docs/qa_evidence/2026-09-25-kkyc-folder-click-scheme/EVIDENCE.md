# #KKYC — folder and file click scheme, second pass

Commit: `2485f9b39cebb785c52ea8d60530ed001ee058f3` on `main` (2026-09-25), verified by the
land.py build gate in `/tmp/claude-1000/land/verify-slots/`.

## What was checked

The owner's scheme for folders — plain click opens the explorer, Ctrl+click and right-click open
the click menu, Alt+click navigates the pane's shell there, Shift+click opens externally — and the
same modifiers on files (Ctrl+click opens a file click menu instead of editing directly;
Alt+click `cd`s to the file's folder).

## Script evidence

```
$ ctest --test-dir build -R "^backends$|^relay-engine-tests$"
1/2 Test  #67: backends .........................   Passed    0.00 sec
2/2 Test #113: relay-engine-tests ...............   Passed   32.70 sec
100% tests passed, 0 tests failed out of 2
```

- `tests/backends_test.cpp` — `folderClickFollowsTheModifiers` covers the full modifier table
  (mouse and keyboard walk, Ctrl>Alt>Shift precedence); `folderClickMenuNamesBothChoicesAndTheirChords`
  and the new `fileClickMenuNamesTheChords` check the menus teach `Click` / `Alt+click` /
  `Shift+click` and grey `navigate` without a shell and `edit` without an editor; the terminal
  right-click menu tests assert the path entries are gone (paths open their own menus).
- `engine/tests/ViewTest.cpp` — the new `altClickCarriesItsModifier` asserts an Alt+click on a
  link emits `linkActivated` with `Qt::AltModifier` (both engine cores, 77/77 ViewTest functions).
  The engine change is the one line adding `Qt::AltModifier` to the immediate link-activation
  mask in `engine/view/TerminalView.cpp` (Alt away from a link still starts a rectangular
  selection).

Not covered by script: the menus opening at the pointer and the explorer pane appearing — that is
the person check in the card's `## Done means`.
