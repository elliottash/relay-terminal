---
id: XY13
type: work
status: needs-verification
labels: [bug, tests, scratch]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 1f8636de-3949-4f85-ab45-2824412ec7bf
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: Scratch watcher in a Claude Code guest pane (69d471f7), measured 2026-09-25 11:30
links: {plans: [], commits: [b37a9e54, b5a36996], evidence: [tests/test_browser_tmpdir.py], related: [DVV2, H1BS, Y4RX, 27AR], github: null}
---
# Orphaned headless Chrome from browser tests runs for days and fills /tmp with com.google.Chrome.* dirs (32k, 142 MB)

## Issue
These unledgered entries in the temp dir or directly under your home directory appeared during your turn: /tmp/com.google.Chrome.3xSfQS, /tmp/com.google.Chrome.4W9bnW, /tmp/com.google.Chrome.chrome_chrome_url_fetcher_.1YQJm0 … and 20 more

## Done means
A test that goes through `tests/browser.py` cannot leave Chrome behind: when the test process dies — SIGKILL, SIGTERM, OOM, a closed pane — its Chrome dies with it, and a normal or excepting exit stops it through `finally`/`stop()` with Chrome's private tmp dir (#H1BS) removed. Checked by: SIGKILL a browser-holding test subprocess and `pgrep -af 'google-chrome.*headless'` shows nothing a few seconds later; `python3 -m unittest tests.test_board_view` leaves no new `/tmp/com.google.Chrome.*` or `/tmp/chrome-*`; the one-time cleanup (four orphan Chromes, the 32k dirs) is done and the `/tmp` count stays flat through a day of test runs. It failed if the evidence's shape returns: a headless Chrome with systemd --user as parent, or `com.google.Chrome.*` counts climbing again.

## Plan
**Goal.** A killed test run must take its Chrome down with it, and Chrome's temp dirs must stay bounded. #H1BS (commit `7ce5c2ef`, in verification) already gives each Chrome a private short TMPDIR that `stop()` removes — the remaining hole, measured in the evidence, is that *nothing runs `stop()`* when the test process itself is killed. Close that hole in `tests/browser.py`, prove it, then do the one-time cleanup of the four orphans and the 32k dirs.

**Findings.**
- `tests/browser.py` is the only launcher of Chrome in the tests (every browser test imports `Browser` from it): `Browser.start()` (~:87) calls `subprocess.Popen` (~:117) with no `preexec_fn`/session kwargs, so Chrome survives its parent's death and is reparented to systemd --user — the four measured orphans. `_chrome_tmpdir()` (:49, from #H1BS) and the retrying `_remove` (:227) already bound the *dir* leak whenever `stop()` runs.
- Every caller uses `try/finally: await browser.stop()` (e.g. `tests/test_board_view.py` ~30 sites) — the gap is process death, not a missing `finally`. SIGKILL runs no Python at all; SIGTERM kills the interpreter before `finally`/`atexit`. Only the kernel can help there.
- The evidence's "own process group" suggestion would *widen* the hole: a Chrome in its own session survives a kill of the test's process group (Ctrl-C, terminal close, group-kill timeouts). The fix that matches the observed failure (parent gone) is `PR_SET_PDEATHSIG` **plus** staying in the test's group, so both single-pid kills and group kills take Chrome down.
- The orphans also sit in `app-com.google.Chrome-*.scope` units outside every pane cap (#Y4RX) — one more reason they must die with the test.

**Steps.**
1. `tests/browser.py`: add a Linux-only module-level `_set_pdeathsig()` preexec helper — cached `ctypes` `prctl(PR_SET_PDEATHSIG, SIGKILL)` (cache the libc function at import; keep the preexec body allocation-free; include the `os.getppid()` race guard so a parent that died between fork and prctl still ends the child). Pass it as `preexec_fn` in `Browser.start()`'s Popen; on non-Linux, no kwarg (documented: macOS keeps `atexit`-only). Comment why *not* `start_new_session`, and that Popen must stay on the event-loop thread (PDEATHSIG fires on the death of the spawning *thread*).
2. Same file: a module-level `_LIVE` list plus `atexit.register` of a sync `_kill_all()` — `start()` appends the Browser, `stop()` discards it (idempotent). The handler terminate→wait(5)→kill's each live process and rmtree's its profile and tmpdir with a *sync* retry sibling of `_remove` (no loop to await in atexit). This covers `sys.exit()` paths that skipped `finally`; SIGKILL is PDEATHSIG's job.
3. Same file: add `__aenter__`/`__aexit__` to `Browser` (4 lines, calls `start`/`stop`) so new tests get the safe form; do not rewrite the ~30 existing try/finally sites.
4. `tests/test_browser_tmpdir.py` (extend; no Chrome needed): (a) PDEATHSIG end-to-end — a disposable `python3 -c` imports `tests.browser`, Popen's `sleep 30` with the same preexec helper, prints the pid and exits; assert the sleep is gone within a few seconds; (b) run `_kill_all()` directly on a Browser holding a real `sleep` Popen and planted profile/tmpdir dirs → process dead, dirs gone; (c) `__aexit__` calls `stop()`; (d) the existing tmpdir cases stay green.
5. Real-Chrome verification: `python3 -m unittest tests.test_board_view` green with `ls -d /tmp/com.google.Chrome.* /tmp/chrome-* 2>/dev/null` empty afterwards; then the orphan repro — start a browser-holding test in a subprocess, `kill -9` it, and assert the Chrome it launched (found by its `--user-data-dir`) is gone within ~3 s.
6. One-time cleanup, in a Run turn and on the owner's authority (this is the evidence's own proposal, now owner-directed): re-measure first — `pgrep -af 'chrome.*headless'` and `ls -d /tmp/com.google.Chrome.* | wc -l` (the 11:30 pids are stale) — then kill only Chromes whose cmdline matches the tests' launch (`--headless=new --disable-dev-shm-usage --user-data-dir=/tmp/tmp…` or a scratch-tmp path), read each cmdline before killing; then `find /tmp -maxdepth 1 \( -name 'com.google.Chrome.*' -o -name 'chrome-*' \) -mmin +60 -delete`. Record before/after counts on the card as evidence.
7. Land: `python3 scripts/land.py begin <me> tests/browser.py tests/test_browser_tmpdir.py` then `commit` (Python-only; land.py byte-compiles, no C++ verify build).

**Risks.**
- `preexec_fn` in a threaded process (asyncio's `to_thread`) has the classic fork-safety caveat — mitigated by the cached, allocation-free preexec body; if it ever proves flaky, the fallback is dropping the atexit half and keeping PDEATHSIG, never the reverse.
- PDEATHSIG is Linux-only; CI and this machine are Linux, macOS dev machines keep today's behaviour plus atexit. Both said in a comment, not left to be rediscovered.
- A hard-killed run can still leave one small `chrome-*` dir behind (nothing of ours runs to remove it); bounded, per-run, inside ledgered scratch when run in a pane — and the #DVV2 sweep flags it. Not worth a watchdog.
- Step 6 kills processes and deletes dirs: verify each cmdline before killing, only the two name patterns older than 60 min are deleted. No orchestration — one agent, two files plus the cleanup.

## Tests
What was built, and where it departs from the plan:
- #H1BS's `b37a9e54` (another session, 11:39) had already given Chrome a private TMPDIR, put it in its own session and made `stop()` end the whole group. So the dir leak was closed whenever `stop()` runs. At 12:30 there were 0 headless Chromes and 0 `/tmp/com.google.Chrome.*` dirs, so step 6's cleanup had nothing left to do and nothing was killed or deleted here.
- b5a36996 closes the remaining hole with `setpriv --pdeathsig KILL --` (util-linux) in front of Chrome's argv instead of a ctypes `preexec_fn`. setpriv sets the flag and execs Chrome in place, so the pid is unchanged, `_end_group` still works, and no Python runs in the forked child of this threaded process. Keeping b37a9e54's own session is safe because PDEATHSIG also covers a group kill: the test dies, and so does Chrome. The atexit handler and `__aenter__` in the plan were not added, because SIGTERM and SIGKILL are both covered by the kernel now and every caller already uses `finally: stop()`. Where setpriv lacks `--pdeathsig` (probed once), the launch is as before.
- `tests/test_browser_tmpdir.py::DiesWithItsTestTests`: a stand-in test starts `sleep 60` exactly as `Browser.start()` starts Chrome, then SIGKILLs itself. The guarded child is gone within 3 s, and the same child without the launcher is still alive, reparented. That control is what shows the test covers the cause. 5/5 OK.
- Real Chrome: a process that ran `Browser().start()` then SIGKILLed itself; its Chrome (pid 66580) was gone 3 s later. `tests.test_remote_browser_storage tests.test_web_viewport`: 13 OK, with 0 Chrome dirs and 0 headless Chromes before and after.
- Not run: `tests.test_board_view` in full, and the day-long flat-count check in Done means.
