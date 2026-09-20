# One face, and one letter, for every button on an action row — #PBX1, 2026-09-20

Owner, of the Switchboard agent's head row: *"make the buttons consistent, can you use the styling
from the card agent"* — and then, of the colours: *"(not the colors though)"*. And: *"it should
have the letter hotkeys for each switchboard action as well"*.

This run drives a real Relay under Xvfb on the binary `land.py` built from the exact tree it
committed (`/tmp/claude-1000/land/actionface/verify/build/relay`, commit `3ed92250`), not this
checkout's `build/relay`, which carries other sessions' in-flight edits. Isolated HOME / XDG_* /
TMPDIR, `RELAY_KEYRING=off`, and no provider account: the one endpoint in the profile is
`stub-provider.py`, so every agent in the run is that script.

The driver is the previous run's, unchanged — `../2026-09-20-action-rows-left/drive.sh` — because
its twenty-two checks are exactly the ones this change must not break:

```
../2026-09-20-action-rows-left/drive.sh \
    /tmp/claude-1000/land/actionface/verify/build/relay \
    "$PWD"
```

**22 checks, 22 passed** (`notes.txt`, one line each, naming the shot it was read from). Every one
is a measured x position, because "left-aligned" is a claim about geometry.

## What changed, measured

`_row-before-after.png` is the head row and the card page's row, cropped from this run and from the
previous one (`69fee7b2`) at the same coordinates, with each button's bounding box in pixels. The
boxes are measured, not eyeballed: `convert … -threshold 12% -connected-components 8`.

| | before `69fee7b2` | after `3ed92250` |
|---|---|---|
| Check | 78 × **33** | 97 × **33** |
| Clean up | 95 × **33** | 116 × **33** |
| Tests | 48 × **25** | 71 × **33** |
| Profile | 57 × **25** | 80 × **33** |
| Plan (card page) | 76 × **30** | 76 × **33** |
| Execute (card page) | 97 × **30** | 97 × **33** |

Tests and Profile were eight pixels shorter than their neighbours and sat four pixels lower in the
row, because they had no stylesheet rule at all and painted the bare Fusion button. They are now
the same height, the same border, the same radius and the same type as Check and Clean up — the
row reads as one toolbar. The card page's two grew three pixels: that is Qt's `QSize(3, 3)` fudge
for a styled `QToolButton` ("### broken QToolButton", `qstylesheetstyle.cpp`) handed back to the
push buttons, which is what makes the two rows one height. Their widths are unchanged.

The colours were **not** made uniform. In the after strip, `Execute (x)` still wears the agent's
violet outline and `Plan (p)` the plain ground; Check, Clean up, Tests and Profile are all plain.
The shared rule declares no colour at all, so each button keeps whatever its own rule gives it.

## What each shot shows

| Shot | What it shows |
|---|---|
| `01-switchboard-idle.png` | The Switchboard beside a terminal pane. The head row is `Check (k) · Clean up (u) · Tests · Profile` from x=782, on the pane's own left margin (x=780, read off the INBOX header), one height and one face. The OCR row reads `782:Check 825:(k) 885:Clean 926:up 946:(u) 1006:Tests 1085:Profile`. |
| `02-turn-running.png` | The same panel mid-turn: the busy strip still reads `✦ Switchboard agent · 0:00  Requesting …` with `✕ Stop`. Nothing about the row's face touched the strip. |
| `03-switchboard-conversation.png` | After the turn, the head row is unchanged — the same four buttons from x=782. |
| `04-card-page.png` | A card page at 1500 px. `Plan (p)` at x=779 and `Execute (x)` at x=861, the margin at x=778: the row is where and what it was, three pixels taller, and Execute is still the outlined one. |
| `05-narrow-card-page.png` | The same at a ~350 px pane: `Plan (p)` at x=394 against a margin of x=393, both labels whole with their keys. |
| `_row-before-after.png` | The two rows, before and after, with the measured boxes. |

`_approvals.png`, `_board.png` and `_list.png` are the run's navigation shots (the first-launch
approvals pane and the list each card was opened from), kept so the path through the app is on the
record.

## The letters

`Check (k)` and `Clean up (u)` carry their keys in the label, the way `Plan (p)` and `Execute (x)`
already did, and the OCR in `notes.txt` reads both suffixes off `01-switchboard-idle.png` — which
is what makes them discoverable where it matters, on the button itself. `k` and `u` were free on
that page: it already spends n, e, p, x, v, m, c, y, t, a, o and `/`.

The list page's key line under the rows also grew `k check` and `u clean up`, appended from the
row itself rather than written out, so a session that adds a keyed button gets its entry free. At
this window width that line is clipped after `Alt+Shift+←→ status` — `QLabel#boardKeys` is
deliberately "clipped in a narrow pane, never widening it" (`src/BoardPane.cpp`), and it already
clipped `m`, `/`, `a`, `t`, `y`, `o` and `Ctrl+Z` before this change. The buttons' own labels are
the surface that does not clip.

The key path itself is covered by `tests/boardmodel_test.cpp::everyActionOnTheSwitchboardsRowHasALetter`
— `k` sends `board_check`, `u` sends `board_cleanup`, a session's own letter clicks its own
button, and the mouse click teaches the letter while the key press does not.
