---
id: DSKT
type: work
status: executing
labels: [bug, tests, flaky, agents]
assignee: agent
rank: zzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [d25ab676e527], evidence: [], related: [8ABD, 234Z], github: null}
---
# Tests find their child processes with pgrep and leave orphans: a probe matched other agents' `sleep 30` loops and poisoned a whole flake hunt

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** Session `3f4a20ad` (#234Z), case 2 of `tests/234z_cases.h`. The test ran `bash -c 'trap "" INT; sleep 30'` in a pane and checked the SIGTERM with `pgrep -f 'sleep 30'`. First the probe matched its own `/bin/sh -c` command line (fixed with `[s]leep 30`). Then it matched **other agents' watcher loops** (`until ! pgrep …; do sleep 30; done`) that live on this machine indefinitely, so the kill worked and the check still said "alive". Failing runs also left five orphaned `sleep 30` processes, which poisoned later runs. The fix was a unique marker in the command line (`sleep 30 # 234z-two-press`). About 10 of the flake hunt's 23 minutes went here.

On a machine where ~20 agents run tests at once, any test that finds processes by name is testing the machine, not the code.

**How to address.**
1. Tests never find their own processes by name. The harness returns the pid/pgid it started (the pane knows its shell's pid and the tty's `tpgid`), and liveness is `kill(pid, 0)` on that pid.
2. Every test-spawned program runs in its own session/process group, and the harness kills the group in teardown, including on assertion failure, so no orphans survive a failed run.
3. A lint in `tests/` (python, cheap) that flags `pgrep`/`pkill`/`killall` in test sources.
4. Fix the existing uses: `rg -n 'pgrep|pkill' tests/`.

**Done means.** No `pgrep`/`pkill` in `tests/` except the lint's allow-list; a failing console-mode case leaves no child process behind (checked by the harness at exit).

## Done means
No test in `tests/` finds or kills a process by name: `rg -n 'pgrep|pkill|killall' tests/` returns only the lint's allow-listed lines, each entry naming why it is not a name-matched kill.

The `234z` case identifies its program by the pid/pgid the pane reports and checks liveness with `kill(-pgid, 0)`, so other agents' `sleep 30` loops on this machine cannot make it pass or fail — proven by running `--234z-only` beside a decoy `until ! pgrep sleep; do sleep 30; done` loop.

A console-mode run leaves nothing behind: the runner SIGKILLs every process group a case started at teardown (including when a CHECK fails) and fails the run at exit if a tracked pid is still alive.

Failure symptom if undone: a name-matching probe passes or fails with whatever else the machine is running (session `3f4a20ad`'s poisoned hunt), and a failing `--234z-only` run leaves `sleep 30` orphans visible in `ps`.

## Plan
**Goal.** Tests in `tests/` identify and clean up the processes they start by pid/process group, never by name, so a machine running ~20 agents cannot make a test pass or fail by coincidence, and a failing run leaves no orphan behind.

**Findings.**
- `tests/234z_cases.h` case 2 (lines 55–91) starts `bash -c 'trap "" INT; sleep 30 # 234z-two-press'` via `h2kqRun` and probes death with `QProcess::execute("/bin/sh", {"-c", "pgrep -f '[s]leep 30 # 234z-two-press' …"})` — the only real name-matching use in `tests/`. The `# 234z-two-press` marker is the interim mitigation for exactly this card.
- The pane already reports what the probe needs: `Pane::shellPid()` and `Pane::foregroundProcessId()` are public (used by `tests/consolemode_test.cpp`'s first case; published on the Host seam, `src/AgentHost.h:78-79`), and #234Z added `Pane::foregroundProcessGroup()` — the tty's `tpgid` read from the shell's `/proc` stat — for its kill. `tests/speech_test.cpp` is the model for pid liveness: `QTRY_VERIFY_WITH_TIMEOUT(::kill(pid, 0) != 0, …)`.
- `tests/consolemode_test.cpp` `main()` has no teardown: panes are stack objects per case, `CHECK` failures count and continue, and every `--234z-only`/`--h2kq-only`/`--xcxd-only` path returns straight after its cases — which is how the failing runs left five `sleep 30` orphans. The pane's shell normally dies with its systemd scope (`src/PaneRuntime.cpp:903-917`, `KillSignal=SIGHUP`), but the "no systemd user session" fallback leaves nothing to stop it, so the harness must.
- `rg -n 'pgrep|pkill|killall' tests/` also hits `tests/test_router.py:35` — a fixture *string* listing common command names for routing classification, not an invocation. It belongs on the lint's allow-list.

**Steps.**
1. `tests/234z_cases.h` case 2: once the busy strip settles, wait (with a deadline) for `pane.foregroundProcessId()` to name the program, remember its pgid (`::getpgid(pid)`, or `foregroundProcessGroup()` — expose that on Pane/Host if it is still private), and replace the `pgrep` loop with a group-gone check `::kill(-pgid, 0) != 0` (ESRCH = the group is empty). Drop the `# 234z-two-press` marker from the command line. Keep the wait loop in the file's existing shape — #8ABD's `waitUntil` will absorb it later.
2. `tests/consolemode_test.cpp`: a process registry — `harnessTrack(shellPid)` called where shell cases build their `Pane` (or inside `h2kqRun`/the run helper every shell case goes through) — plus an RAII guard per case that SIGKILLs `-(getpgid(pid))` on destruction so a failed CHECK still cleans up; at every `return` in `main()`, rescan with a short deadline and fail the run (`FAIL orphaned pid …`) if a tracked pid still answers `kill(pid, 0)` after SIGKILL.
3. New `tests/test_no_name_matched_processes.py`: scan `tests/` for `\b(pgrep|pkill|killall)\b` (pattern built from pieces so the lint's own source is not a hit) and fail, with an allow-list of (path, must-still-match snippet) pairs seeded with `tests/test_router.py`'s fixture line; a stale entry — snippet no longer in the file — fails too.
4. Re-run `rg -n 'pgrep|pkill|killall' tests/` — expect only allow-listed lines.

**Risks.**
- Zombies answer `kill(pid, 0)`; the case-2 probe checks the *group* and the at-exit scan treats "alive after SIGKILL plus deadline" as the orphan criterion, not a bare probe.
- The foreground pid lands one shell-poll beat after the stop strip; waiting for it is this card's business, the wider wait rework is #8ABD's. Both cards edit `tests/234z_cases.h` and `tests/consolemode_test.cpp`: claim paths with `scripts/land.py begin`, re-read the other card's hunks if it lands first.
- Exposing `foregroundProcessGroup()` is a one-line visibility change on Pane/Host, not a behaviour change; the product kill that uses it (#234Z) is untouched.

**Verify.**
- `scripts/relay-build --target relay-consolemode-tests`, then `ctest --test-dir build -R 'consolemode|queuecontract'` and `python3 -m pytest tests/test_no_name_matched_processes.py`.
- Machine-independence: with a decoy `until ! pgrep sleep; do sleep 30; done` loop running in another shell (the loop that poisoned session `3f4a20ad`), `./build/relay-consolemode-tests --234z-only` still passes.
- Orphans: after `--234z-only`, `ps -eo pid,pgid,cmd | grep '[s]leep 30'` shows nothing and the at-exit scan prints nothing.
