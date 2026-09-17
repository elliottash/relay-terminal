# Reopen where I left off: implementer evidence (2026-09-17)

Not a QA verdict. Implemented by Claude Opus 5 (Claude Code) in the restore-windows worktree.
Issue: `issues/features/needs_qa_llm/2026-09-17-restore-windows-on-start.md`.

## Automated

- `cmake --build build` from a clean tree: **no warnings**.
- `ctest --test-dir build`: **10/10** (new: `windowstate`, 22 cases in `tests/windowstate_test.cpp`).
- `./scripts/test.sh`: **376 tests, all pass** — the worker needed no change, because `resume`
  already accepts `session_id` next to a request `id`.

## Live run

`./drive.sh` — Xvfb `:93` (1600×1000), xdotool, ImageMagick `import`, isolated
`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`/`XDG_RUNTIME_DIR`. `RELAY_KEYRING=off` so the
desktop keyring is never touched, and the provider "key" is the literal string
`offline-demo-not-a-key`, which is enough for the worker to configure an agent and create a session
store. **No API key exists in this run and nothing reaches the network** — no agent turn is sent,
and the only conversation is a hand-written fixture.

Five app starts:

| Run | What it does | Result |
|---|---|---|
| 1 | Build the layout: window 1 = split (alpha ∥ beta) in tab 1 + a `gamma` tab, back on tab 1; window 2 = one pane in beta. Then `kill -TERM` (no `aboutToQuit`, so only the debounced write counts). | `windows.json` 0600, `version 1`, 2 windows, geometry + screen, tabs 2/1, `current 0`, titles `['beta  ·  2', 'gamma']` / `['beta']` — `windows-after-run1.json` |
| 2 | Plain `relay`, no arguments. | **2 windows reopened**, same tabs, same splits and sizes, same directories; the alpha pane prints `Session loaded: "Where I left off" · 0 turn(s)`, the three panes with no saved conversation print `Previous conversation is no longer saved · starting a new one`. Then the palette shows `✓ Reopen windows on start` and runs **Start a fresh window set**, which deletes the file. |
| 3 | `relay --workspace …/alpha` after the file was cleared. | 1 window, 1 pane |
| 4 | From run 3: Ctrl+N, then close one window (confirm), then close the last one (confirm → Relay quits). | after closing one: **`saved windows: 1`**; after the quit: **`saved after quit: 1`** — a window you close stays closed, quitting keeps what was open (`windows-after-run4.json`) |
| 5 | `relay --fresh`. | **1 window**, and `saved layout untouched by --fresh: 1` |

Screenshots `implementer-01` … `implementer-14`; the interesting ones are `05`/`06` (both windows
back, resumed conversation line), `07` (the `gamma` tab), `08`/`09` (the two controls in the
palette), `13` (the close confirmation) and `14` (`--fresh`).

## Two headless checks (offscreen, no X needed)

`./two-instances.sh` → `two-instances.txt`. Relay A starts and saves one window and takes
`$XDG_RUNTIME_DIR/relay/windows.lock`; the file is then marked by hand (two windows); Relay B starts
against the same profile, runs, and quits. The file still has **two** windows, so B never wrote —
only the lock owner saves.

```
A saved: 1 window(s)
lock file exists
marked: 2 window(s)
after B ran and quit: 2 window(s)  (2 = B left it alone)
```

`./broken-file.sh '<json>'` → `broken-file.txt`. A foreign-version and a malformed
`windows.json` each produce one stderr line and a normal single window, and the file is replaced
with a valid version-1 layout on the next save:

```
relay: Saved window layout ignored: The saved window layout is version 99; this Relay writes version 1.
relay: Saved window layout ignored: unterminated object
```

## Things a QA session should know

1. **`· 0 turn(s)` in the restored pane is a fixture artefact, not a bug.** The worker counts turns
   from the session's checkpoints, and the hand-written fixture has none. The messages did load:
   the pane's context meter reads `ctx 4.8k` where a fresh pane reads `ctx 0`. With a real saved
   conversation the line reads the real count. Re-check this with a real provider key.
2. **The `gone session` note is exercised three times** in run 2, because a pane that never
   completed a turn has a session id but no session file yet. That is the intended behaviour, not a
   failure of the restore.
3. **`kill -TERM` in runs 1 and 2** is deliberate: Qt does not run `aboutToQuit` for it, so the file
   that run 2 reopens is purely what the 1 s debounce had already written — the "a crash loses at
   most the debounce window" guarantee. Run 4 covers the graceful close/quit path instead.
4. **Screens and Wayland are not covered here.** Xvfb has one screen named `screen`, and the run is
   X11. `clampToScreens()` is covered by four unit tests; the Wayland path (size only, compositor
   places the window) still needs a real Wayland session.
5. **Two instances** are covered headlessly by `two-instances.sh` (the write side). What it does not
   show is the status-bar line the second instance prints, or taking the lock over after the owner
   quits — checklist step 8 in the issue covers those.
6. The `titles` in the file are informational — Relay recomputes tab titles from the panes on
   restore. `beta  ·  2` is the first tab because its last active pane was the beta one and it holds
   two leaves.
