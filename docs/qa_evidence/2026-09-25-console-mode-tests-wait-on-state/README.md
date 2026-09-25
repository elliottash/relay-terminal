# QA evidence — #8ABD console-mode tests wait on pane state

Date: 2026-09-25. Implemented by this session (pane b913fef9, model moonshotai/kimi-k3).
Commits: `3346744` (cases + shared waits + hook), `6bb47dd` (runner half re-landed after a
concurrent write dropped it from the first landing).

## What the tree was

All runs below are on a clean `git archive` export of `main` at the landed tip
(`3346744` for the repeat runs, plus `6bb47dd`'s consolemode_test.cpp for the runner) with
`-DCMAKE_BUILD_TYPE=RelWithDebInfo`, `QT_QPA_PLATFORM=offscreen`. The host had three other
sessions building concurrently during the timing runs.

## Done means, checked

| Item | Result |
| --- | --- |
| `h2kq`/`234z`/`xcxd` wait on pane state through `tests/pane_waits.h` | ✔ (see rg below) |
| no hand-rolled `for (i < N) xcxdPump(25)` loops in the three case headers | ✔ |
| `--234z-only --repeat 20` passes 20/20 | ✔ 20/20 |
| … in under 30 s | ✘ 42.5 s wall (breakdown below) |
| `--h2kq-only --repeat 20` spot check | ✔ 20/20 (70 s) |

`rg -n 'for \(int i = 0; i < [0-9]+ &&' tests/h2kq_cases.h tests/234z_cases.h tests/xcxd_ui_cases.h`
returns nothing.

## Numbers

- `--234z-only` single: pass, ~3 s (was 125–189 s per run in session #234Z's flake hunt).
- `--234z-only --repeat 20`: 20/20 passed, 42.5 s wall, 32 s user. Per-iteration ~2.0 s.
- `--h2kq-only --repeat 20`: 20/20 passed, 70 s (6 h2kq cases per iteration).
- `--xcxd-only`, `--model-queue-only`, `--composer-only`, `--recall-only`: all passed, 0 fails.
- Default (no-arg) suite: fails 5 checks — all pre-existing on plain `main` (verified on a clean
  export of the tip before #8ABD's changes): `ctrlClickEditsTheActualFile` (3 checks) and two
  turn-summary spacing checks. Filed as #DJ3X.

## Why the 30 s line is missed

Instrumented per-case timing (RELAY_REPEAT_VERBOSE=1) on the 234z pair, per warm iteration:
~0.6 s pane spawn + submit + strip wait; ~0.3 s busy-name resolution (the engine's, not the
harness's); a mandated 650 ms beat between the two Alt+Esc presses (the pane's >600 ms arming
rule, `Pane.h` forceInterruptShell); ~0.3 s kill wait + teardown. The waits themselves end as
soon as the state holds; the floor is two real pane spawns, two shell submits and the arming
beat. Options if the owner wants <30 s: pane reuse across the two cases (blocked by the arming
beat needing >600 ms between the ssh and bash cases' presses), a quieter host, or lowering the
repeat count.

## Flakes the waits surfaced (and their fixes, in the commits)

1. The two-press case pressed while bash was still installing `trap "" INT`: the pane was busy
   (tpgid flipped) but the press killed the program outright. Fixed by waiting for the busy line
   to name `sleep` before pressing.
2. The polite-press assertion used the strip's stop control, which is layout and can hide behind
   the press's toast; the fact is `pane.processBusy()`. Fixed by asserting the fact.
3. Forcing `rebuildQueueStrip()` at the waits' 25 ms cadence churned the strip lanes
   (`shellList->count() != 1`, stop control transiently hidden under `--xcxd-only`);
   `pollPaneStatusNow()` therefore runs `pollProgram()` only and lets the strip follow the fact
   through its own rebuild points.
