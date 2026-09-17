# Clickable paths (#YZTK) and the keyboard walk over them (#GWXM) — implementer evidence

Implemented by Claude Opus 5 (Claude Code session), 2026-09-17. **Not a QA verdict.**

Everything here was produced by `drive.sh` in this directory, once per engine:

```sh
docs/qa_evidence/2026-09-17-clickable-paths/drive.sh "$PWD/build"           # Relay engine (default)
docs/qa_evidence/2026-09-17-clickable-paths/drive.sh "$PWD/build" konsole   # KonsolePart
```

Xvfb (the script takes the first free display in `:140`-`:180`), 1500x950, no window manager, isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`
(so no provider keys and no first-run dialogs), Qt5/KF5 build, libvterm core (no libghostty-vt
prefix on this machine).

Two things about capturing on this host:

- **Root-window captures come back black.** Every screenshot is `import -window "$win"` of the
  Relay window itself. Relay owns several X windows (a 1x1 helper, an 8x19 one, the tooltip);
  the script picks the largest visible one.
- **The tooltip is its own override-redirect X window**, so it does not appear inside a capture
  of the Relay window — there is a black rectangle where it sits. It is captured separately as
  `implementer-relay-05-hover-tooltip.png`.

## Relay engine (`implementer-relay-*`)

| Shot | What it shows |
|---|---|
| 01 | `ls`: `code.py fakebuild.sh 'my file.txt' notes src tests` |
| 02 | `grep -n needle notes/notes.txt` → `2:beta needle gamma`, then a Python traceback with two `File "…/code.py", line N` frames |
| 03 | cargo-style `--> src/main.cpp:3:24`, tsc-style `src/main.cpp(3,24): error TS2304`, and `ls -l my\ file.txt` → `'my file.txt'` |
| 04 | hover over `src/main.cpp:3:24`: it is underlined (the black rectangle is the tooltip window) |
| 05 | the tooltip itself: `<workdir>/src/main.cpp:3` — the resolved absolute path and the line |
| 06 | **plain left click** on the folder `notes` in the `ls` row → an explorer pane opened on `<workdir>/notes` |
| 07 | **Ctrl+click** on `src/main.cpp:3:24` → a preview pane opened on `main.cpp` |
| 08 | F12 back to the prompt box (the composer is visible and has the focus) |
| 09 | **Ctrl+Shift+L** with the prompt box focused: the status line reads `15 of 15 · <workdir>/my file.txt · Enter opens, Esc leaves` and the quoted name **with the space**, wrapped across two rows, is highlighted |
| 10 | three presses of **Up**: `12 of 15 · <workdir>/src/main.cpp:3`, with `src/main.cpp:3:24` highlighted and underlined |
| 11 | **Enter**: `main.cpp` opened in the preview pane and the highlight is gone |
| 12 | final state: terminal + explorer + preview in one tab |

## KonsolePart (`implementer-konsole-*`), through the existing `relay-open` helper

| Shot | What it shows |
|---|---|
| 01-03 | the same output |
| 04 | KonsolePart's own file underline on `code.py` |
| 06 | plain click on the folder `notes`: nothing opens — KonsolePart only activates a hotspot on Ctrl+click, and folders go to KIO, not to Relay (known gap, `docs/ARCHITECTURE.md` section 10) |
| 07 | **Ctrl+click** on `code.py` → Relay's preview pane opened on `code.py`, i.e. the profile's `TextEditorCmdCustom=relay-open PATH:LINE:COLUMN` path still works |
| 09-11 | **Ctrl+Shift+L** reports `This pane's engine cannot read the screen; start a pane with the Relay engine to step through links.` and nothing else happens (the three shots are identical, as they should be) |

There is no `implementer-konsole-05-hover-tooltip.png`: KonsolePart underlines the file itself and
shows no Relay tooltip, so no tooltip window existed to capture.

KonsolePart 23.08 also does not underline the `:line:column` suffix (`src/main.cpp:3:24` is not a
hotspot at all, only `src/main.cpp` would be), which is why the Konsole run clicks a plain path.
Verified separately that `src/main.cpp` is underlined and `src/main.cpp:2` is not.

## Automated checks

- `cmake --build build` and a clean configure+build in a fresh directory: no warnings.
- `./scripts/test.sh`: 508 tests OK.
- `ctest --test-dir build`: 17/17 (the 16 groups that existed before, plus the new `outputlinks`).
- New tests: `tests/outputlinks_test.cpp` (25 cases over the formats, the false positives and the
  cursor) and two cases in `engine/tests/ViewTest.cpp` (`keyboardLinkWalk`, `plainClickFollowsAPath`).
