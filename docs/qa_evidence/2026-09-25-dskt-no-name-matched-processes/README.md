# #DSKT — tests find their child processes by pid, never by name

Evidence for card #DSKT. Session d1d07d15 (glm-5.3), 2026-09-25. Commits: `d25ab676` (harness +
atexit orphan scan + review-pane guard) and the follow-up landing the 234z probe swap, the
remaining guards and `tests/test_no_name_matched_processes.py` (see `links.commits` on the card).
The 234z probe swap and that file's guards were written on top of #8ABD's uncommitted wait restyle
of the same block, and landed inside #8ABD's `33467449` with it; the follow-up carries the guards in
`h2kq_cases.h` and `xcxd_ui_cases.h`, the lint and this evidence.

## What changed

- `tests/consolemode_test.cpp`: `namespace harness` — a pid registry, `harness::ProcessGuard`
  (case-scope RAII: SIGKILLs the pane's foreground process group — `Pane::foregroundProcessGroup()`,
  the pty's `tpgid` from #234Z — and the shell's own group at case end, failed CHECKs included,
  since CHECK counts and continues to scope end) and an `atexit` scan that fails the run if a
  tracked pid still answers `kill(pid, 0)` after the guards' SIGKILL plus a 2.5 s reap deadline.
  Registered with `atexit` so every one of main()'s return paths passes through it (#8ABD was
  rewriting main() around `runRepeated` at the same time; the scan needed no main() edit).
- `tests/h2kq_cases.h`, `tests/234z_cases.h`, `tests/xcxd_ui_cases.h`,
  `tests/xcxd_review_cases.h`: every shell-case pane holds a `ProcessGuard`.
- `tests/234z_cases.h` case 2: the `pgrep -f '[s]leep 30 # 234z-two-press'` probe is replaced by
  the pid/pgid the pane reports — resolve `pane.foregroundProcessGroup()` with a deadline after
  the busy fact lands, then after the second Alt+Esc wait for `::kill(-pgid, 0) != 0` (ESRCH: the
  group is empty). The `# 234z-two-press` marker is gone from the command line; the program is
  never found by name.
- `tests/test_no_name_matched_processes.py`: lint banning pgrep/pkill/killall in `tests/`, with a
  self-checking allow-list (`tests/test_router.py`'s fixture string of common command names).

## Verification runs (this machine, ~20 agents active)

- `scripts/relay-build --target relay-consolemode-tests` — builds.
- `QT_QPA_PLATFORM=offscreen ./build/relay-consolemode-tests --234z-only` — **passes with a
  decoy loop running** that keeps a `sleep 30` process alive on the machine for the whole run
  (`until pgrep -f '[x]yzzy-no-such-marker-dskt' >/dev/null; do sleep 30; done`; the marker never
  appears, so the loop holds a `sleep 30` at all times). After the run, the only `sleep 30` on the
  machine is the decoy's own: `ps -eo pid,pgid,cmd | grep '[s]leep 30'` shows no test orphans.
- `--h2kq-only` — all cases passed. `--xcxd-only` — passes; its first run after a build failed
  once at `xcxd_ui_cases.h:227-247` (stop strip not up yet) and passed on immediate rerun; the
  same first-run-after-build flake reproduces on clean tip `134aa033`, i.e. pre-existing and the
  class #8ABD is fixing, not this change.
- Default suite: the only failing checks are the pre-existing `ctrlClickEditsTheActualFile`
  group (`consolemode_test.cpp` 720/721/733 + occasionally 1917/1920) — proven pre-existing by a
  clean-tip build of `134aa033` in a scratch export failing the identical checks (same file, line
  numbers shifted by this change's 87 added lines): 633/634/646 and 1917/1920. Known signal
  `ctest:consolemode` (#VZ8C) tracks the suite.
- **Orphan-scan FAIL path**: a throwaway build of `d25ab676` patched to `track(::getpid())` (a
  pid that cannot die) prints `FAIL orphaned pid <pid> is still alive after the harness SIGKILL`
  and exits 1 although all cases passed — the scan fails the run, not just logs.
- `python3 -m pytest tests/test_no_name_matched_processes.py` — 1 passed.
- `rg -n 'pgrep|pkill|killall' tests/` — only `tests/test_router.py`'s allow-listed fixture line
  (and `ripgrep` mentions, which the lint's `\b`-anchored pattern does not match).

## Machine-independence argument

Case 2's liveness fact is now `kill(-pgid, 0)` on a pgid read from the pane's own pty, so no
command-line content on this machine — another agent's `sleep 30`, a watcher loop, this test's
own `/bin/sh` — can match it. The decoy run above is the direct demonstration: the probe that
session `3f4a20ad` poisoned passes with the poisoning loop running.
