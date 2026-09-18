---
id: T9ZS
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzt9
created: '2026-09-17'
acceptance: Ctrl+? opens the shortcuts overlay on the owner's keyboard, and the overlay lists the key that worked
source: '`issues/bug_intake.txt`, 2026-09-17: "ctrl+? doesnt work to show the shortcuts"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+? does not open the shortcuts overlay

`help.shortcuts` is bound to Ctrl+? and F1. On the owner's keyboard Ctrl+? is Ctrl+Shift+/ and does not fire.
Bind the combination Qt actually reports (check Ctrl+Shift+/, Ctrl+/ and the keypad variants), verify in all
four presets, and make sure the palette entry and F1 still work.

## Cause

"Ctrl+?" is one gesture with several spellings, and only one of them was bound.

A key probe built against this Qt (6.4.2, XCB) shows what actually arrives:

| pressed | Qt key | modifiers |
|---|---|---|
| Ctrl+Shift+/ (main row) | `Qt::Key_Question` (0x3f) | Ctrl \| Shift |
| Ctrl+/ (main row) | `Qt::Key_Slash` (0x2f) | Ctrl |
| Ctrl+Shift+/ (keypad) | `Qt::Key_Slash` | Ctrl \| Shift |
| Ctrl+/ (keypad) | `Qt::Key_Slash` | Ctrl |

`QKeySequence::fromString("Ctrl+?")` is `Key_Question + Ctrl`, so only the first row could match, and only
through `Keymap::match()`'s shifted-symbol fallback (`mods ^ Qt::ShiftModifier` for a symbol key). Whether
that fallback saves the binding depends on which keysym the layout gives the `/` key: a layout (or a keypad)
that reports **`Key_Slash`** with Ctrl+Shift falls back to `Ctrl+/`, which nothing was bound to, so nothing
happened. That is the owner's case, and it is why the same key works on a US layout under Xvfb — the bug
only shows on the layouts and on the keypad where Qt reports the unshifted symbol.

The overlay also never said which key opened it, so a user whose "Ctrl+?" does nothing had no way to find a
key that works: the `?` help card showed only the first binding, "Ctrl+?".

## Implemented

- `src/main.cpp`, action registry: `help.shortcuts` is bound to **Ctrl+?, Ctrl+Shift+/, Ctrl+/ and F1**.
  That covers every combination the table above can produce, directly rather than through the fallback
  (Ctrl+Shift+? still reaches Ctrl+? through it). All four presets inherit it: none of `relay`, `warp`,
  `vscode` or `konsole` overrides `help.shortcuts`, and none of them binds `/` or `?` to anything else.
- `src/main.cpp`, `Keymap::shortcutTexts(id)`: every key bound to an action, as the desktop writes them,
  de-duplicated. Used where one gesture needs several spellings.
- `src/main.cpp`, shortcuts overlay: a footer line above the existing note — "Opens with Ctrl+?, Ctrl+Shift+/,
  Ctrl+/, F1, or from the palette (Ctrl+Shift+A).  You pressed Ctrl+Shift+?." The window records the last
  action run from the keyboard and the combination that ran it (`m_lastShortcut`), so the overlay names the
  key that actually reached it; a palette run never sets it, and it is cleared once read.
- `src/main.cpp`, `?` help card: the row now reads `Ctrl+?  show all shortcuts (also Ctrl+Shift+/, Ctrl+/, F1)`.
- `tests/test_keybindings.py`, `GuiDefaultsTests`: reads the action registry out of `src/main.cpp` and
  asserts `help.shortcuts` binds every spelling and keeps F1, that each key is a valid portable sequence,
  that the list stays within `MAX_KEYS`, and that no other action claims one of those keys.

Build: `cmake --build build` with no new warnings. `ctest --test-dir build` 16/16; `./scripts/test.sh` 511
tests pass.

## Evidence

`docs/qa_evidence/2026-09-17-bugfix-batch1/`, harness `shortcuts.sh <preset>` (Xvfb). Every key is pressed
in turn, with Escape in between, and the overlay is captured by its own window id:

| key sent | relay | warp | vscode | konsole |
|---|---|---|---|---|
| Ctrl+Shift+/ | opened | opened | opened | opened |
| Ctrl+/ | opened | opened | opened | opened |
| Ctrl+Shift+? | opened | opened | opened | opened |
| Ctrl+Shift+KP_Divide | opened | opened | opened | opened |
| Ctrl+KP_Divide | opened | opened | opened | opened |
| F1 | opened | opened | opened | opened |
| palette entry | opened | opened | opened | opened |

- `relay-01-ctrl-shift-slash.png`: the overlay with the footer "Opens with Ctrl+?, Ctrl+Shift+/, Ctrl+/, F1,
  or from the palette (Ctrl+Shift+A).  You pressed Ctrl+Shift+?."
- `relay-08-help-card.png`: the `?` help card naming the other keys.

## QA checklist

1. On the owner's keyboard, press Ctrl+? (Ctrl+Shift+/): the overlay opens, and its footer names the key it
   saw. Press Ctrl+/ and F1 too.
2. Press the same keys on the numeric keypad's `/`.
3. Open the overlay from the palette (Ctrl+Shift+A › "Keyboard shortcuts…") and from Settings › Shortcuts ›
   "Show…": both must open it, and the footer must then name no key ("You pressed" is absent), because no
   key was used.
4. Escape closes the overlay; the search field filters; the list shows "Show all keyboard shortcuts" with
   its keys.
5. Switch `keybindings.json` to each of `warp`, `vscode` and `konsole` (`"preset": "..."`) and repeat 1: the
   keys must work in all four, and the status bar must not report a shortcut conflict when the file reloads.
6. Bind `help.shortcuts` to something else in `keybindings.json` (`"bindings": {"help.shortcuts": ["F2"]}`):
   only F2 opens it, and the footer and the `?` card say F2.
7. Unbind it (`[]`): the palette entry still opens it, and the footer says "Opens with no key".
8. Type `?` into an empty prompt box: the short help card appears, with the "(also …)" note, and `?` hides
   it again. Typing `?` after other text must insert a literal `?`.
9. With a full-screen program running in the focused pane (`vim`), Ctrl+Shift+/ must still open the overlay
   (Ctrl+Shift shortcuts act inside programs under the default `program_keys`), while plain Ctrl+/ must
   reach the program instead.
