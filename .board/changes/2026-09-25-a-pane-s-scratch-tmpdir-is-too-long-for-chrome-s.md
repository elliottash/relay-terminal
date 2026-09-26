---
id: H1BS
type: work
status: needs-verification
labels: [bug, tests]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 5ca366c8-3171-46e7-ac74-6f149a1dfbdd
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: pane 7dbb2c54 (guest Claude Code), measured 2026-09-25
links: {plans: [], commits: [7ce5c2ef, b37a9e54], evidence: [docs/qa_evidence/2026-09-25-h1bs-chrome-tmpdir/tests.txt, docs/qa_evidence/2026-09-25-h1bs-chrome-tmpdir/], related: [DVV2, 7PEC], github: null}
---
# A pane's scratch TMPDIR is too long for Chrome's singleton socket, so the browser tests cannot start in a Relay pane

## Issue
Found while working #7PEC (2026-09-25), measured: in a guest Claude Code pane, TMPDIR=/home/elliott/.cache/relay/scratch/7dbb2c54-9215-4e1f-a044-52c4f6c7dc1a/tmp. `google-chrome --headless=new --user-data-dir=<mktemp -d> about:blank` dies with `FATAL:chrome/browser/process_singleton_posix.cc:313] Socket path too long: /home/elliott/.cache/relay/scratch/7dbb2c54-…/tmp/com.google.Chrome.e7rpEL/SingletonSocket` (a Unix socket path is limited to 108 bytes), so every test that uses tests/browser.py (`python3 -m unittest tests.test_board_view`) fails with "Chrome exited while starting." — 8/8 errors. With TMPDIR=/tmp/claude-1000/7pec the same run is 36/36 OK. The per-session scratch TMPDIR from #DVV2 needs a shorter path (or tests/browser.py a short profile/TMPDIR of its own).

## Done means
Browser tests that launch Chrome start and pass inside a Relay pane with the pane's own per-session TMPDIR in force: `python3 -m unittest tests.test_board_view` is 36/36 with no `TMPDIR=/tmp/...` workaround (the caveat #7PEC had to note in its Tests).

`Browser.start()` keeps Chrome's singleton-socket directory under a path that fits the 108-byte Unix socket limit wherever the inherited TMPDIR points, so the failure cannot come back on a longer HOME.

It fails if the same run errors again with "Chrome exited while starting" / `process_singleton_posix.cc … Socket path too long`.

## Plan
**Goal.** Make every test that goes through `tests/browser.py` able to start Chrome inside a Relay pane, where TMPDIR is the pane's per-session scratch dir — by giving Chrome's process a short TMPDIR of its own whenever the inherited one cannot fit its singleton socket — and, per the owner's decision, also shorten the pane scratch path itself (step 6).

**Findings.**
- `backend/relay_core/agent.py:635` `scratch_tmpdir()` points TMPDIR at `scratch.session_root(key) / "tmp"`, `key` = `RELAY_SESSION_TOKEN` (a 36-char pane UUID) → `/home/elliott/.cache/relay/scratch/<uuid>/tmp` = 74 bytes here (34 + 36 + 4).
- `backend/relay_core/scratch.py:460` `session_root()` = `scratch_root() / _safe_name(session)`; mirrored in `src/AppPaths.h:121` `relay::scratchpaths::sessionRoot` (used by `src/PaneRuntime.cpp:903` and `src/GuestBridge.h:296`). A formula change means all three plus `docs/SCRATCH.md`.
- Chrome's `ProcessSingletonPosix` puts its socket at `$TMPDIR/com.google.Chrome.XXXXXX/SingletonSocket` — up to 40 more bytes; Linux `sun_path` holds 108 incl. NUL. 74 + 40 = 114 → the measured `FATAL … Socket path too long`, and `tests/browser.py` dies with "Chrome exited while starting".
- `tests/browser.py` `Browser.start()` launches Chrome with the inherited environment and never sets TMPDIR for the child. Users: `tests/test_board_view.py` and any other `from tests.browser import` (grep for them all; they all go through `Browser.start()`, so one fix covers them).

**Steps.**
1. `tests/browser.py`: add a module helper `_chrome_tmpdir()` — return `None` when `len(tempfile.gettempdir()) + len("/com.google.Chrome.XXXXXX/SingletonSocket")` fits (limit constant 104, leaving margin under 107), else a fresh `tempfile.mkdtemp(prefix="chrome-", dir="/dev/shm")`, falling back to `dir="/tmp"`, falling back to the inherited dir. Comment cites the 108-byte `sun_path` limit and this card.
2. Same file, `Browser.start()`: when `_chrome_tmpdir()` returns a dir, pass `env={**os.environ, "TMPDIR": …}` to the Chrome Popen (add `import os`); remove the dir in `stop()` next to the profile cleanup. The `--user-data-dir` profile stays where it is — only the singleton socket needs the short path.
3. New `tests/test_browser_tmpdir.py`, no Chrome needed: with a planted long TMPDIR (a 40+-char dir) `_chrome_tmpdir()` returns a dir outside it whose worst-case socket path is ≤ 104; with a short TMPDIR it returns `None`; two long calls return distinct dirs and each cleans up.
4. Reproduce → verify: `mkdir` a UUID-named dir mimicking the pane formula and run `TMPDIR=<it> RELAY_KEYRING=off python3 -m unittest tests.test_board_view` — before the fix this is the 8/8-error reproduction, after it 36/36 OK. Run the same way whatever else imports `tests.browser`.
5. Shorten the session component (owner decision, 2026-09-25 — ship it): `session_root()` in `backend/relay_core/scratch.py` and `sessionRoot()` in `src/AppPaths.h` take the first 12 chars of `_safe_name(session)` ("adhoc" untouched); update `docs/SCRATCH.md`; ledger rows need no migration — they are keyed by absolute path, so a live pane just gets a fresh short root on its next conversation and the old row ages out at `days:7`. Update the `session_root` expectations in `tests/test_scratch_ledger.py` (e.g. `test_session_tmpdir_points_inside_session_root_and_is_ledgered`), run `python3 -m unittest tests.test_scratch_ledger tests.test_scratch`, build via `scripts/relay-build` (land.py's verify slot builds it again), and comment on #DVV2 that the formula moved under its verification.
6. Land per repo procedure: `python3 scripts/land.py begin <me> tests/browser.py tests/test_browser_tmpdir.py backend/relay_core/scratch.py src/AppPaths.h docs/SCRATCH.md tests/test_scratch_ledger.py` then `commit`. The C++ header means land.py's verify build runs — build clean locally first via `scripts/relay-build`.

**Risks.**
- `/dev/shm` may be missing or unwritable in odd environments → the `/tmp` fallback covers it; if both fail, behaviour is today's (inherit), never worse.
- The conditional means machines with a short TMPDIR keep today's behaviour exactly — good; the test in step 3 pins both branches.
- Step 5 changes the #DVV2 path contract while that card sits in needs-verification: land the whole plan in one commit, and the #DVV2 comment from step 5 is not optional.
- Shortening is a big margin, not a guarantee (a long enough `$HOME` can still overflow); the step 1 conditional in `tests/browser.py` is the guarantee for tests.
- No orchestration: one agent, small diffs; every file belongs to this plan.

**Verify.**
- `python3 -m unittest tests.test_browser_tmpdir` — green, and red against the pre-fix `browser.py` semantics (no such helper before).
- `TMPDIR=<74-byte scratch-style dir> RELAY_KEYRING=off python3 -m unittest tests.test_board_view` → 36/36 OK; the identical command before the change died with `FATAL … Socket path too long` / "Chrome exited while starting".
- Step 5: `tests.test_scratch_ledger` + `tests.test_scratch` green, `scripts/relay-build` clean, and a fresh pane's `echo $TMPDIR` shows the short root.

## Tests
Commit `7ce5c2ef`; evidence `docs/qa_evidence/2026-09-25-h1bs-chrome-tmpdir/tests.txt`. TMPDIR below = `/home/elliott/.cache/relay/scratch/7dbb2c54-9215-4e1f-a044-52c4f6c7dc1a/tmp` (the measured pane path, which is the old formula's shape).

- Reproduced before the change: `TMPDIR=$D RELAY_KEYRING=off python3 -m unittest tests.test_board_view` → 36 run, **errors=32** (Chrome exited while starting).
- After: same command → **36/36 OK**, with no `TMPDIR=/tmp/...` workaround.
- `python3 -m unittest tests.test_browser_tmpdir` → 3/3 OK (short TMPDIR kept, long TMPDIR gets a distinct `/dev/shm` dir with a socket path ≤ 104 each time, `Browser.stop()` removes it). Before the change it errors, because `_chrome_tmpdir` did not exist.
- `python3 -m unittest tests.test_scratch_ledger tests.test_scratch` → OK (skipped=1). New `test_session_root_names_a_pane_uuid_by_its_first_12_chars`. `test_session_tmpdir_points_inside_session_root_and_is_ledgered` used to fail inside any Relay pane because the pane's `RELAY_SESSION_TOKEN` won; it now clears that variable itself.
- The other `tests.browser` users under the same long TMPDIR: test_remote_browser 25, test_pane_view 37, test_remote_browser_storage 6, test_remote_terminal 12, test_remote_push 75, test_web_screen 10, test_remote_guest_browser 14, test_web_meet_code 16 all OK. test_web_viewport 7 had 1 error on its first run and was OK on 3 reruns (flaky; not a Chrome-start failure).
- `scripts/relay-build --target relay` built clean, and land.py's verify slot built the landed tree. The full `scripts/relay-build` is red in another session's uncommitted `tests/conversations_test.cpp`, which this change does not touch.
- Not checked here: a fresh pane's `echo $TMPDIR` showing `<scratch>/<12 chars>/tmp`. That needs a pane started from the new binary.

Follow-up commit `b37a9e54`: the browser tests leave nothing behind. A terminated Chrome left `com.google.Chrome.*` lock-socket and url-fetcher folders in TMPDIR (32,377 in `/tmp`). A child process that outlived the browser also rewrote the profile `stop()` had just removed. Now Chrome **always** gets a private TMPDIR (under the inherited one when it fits the limit), runs in its own process group, and `stop()` ends the whole group before removing the profile and TMPDIR. Because the short-TMPDIR case changed, `test_short_tmpdir_is_kept` became `test_short_tmpdir_gets_a_private_dir_inside_it`.
- `tests.test_browser_tmpdir` 4/4 OK, including the new `test_end_group_waits_for_children_that_outlive_the_leader`.
- Each run with a fresh short TMPDIR, then `ls -A` of it: test_board_view, test_web_viewport, test_remote_browser, test_pane_view, test_remote_terminal and test_remote_guest_browser all OK, **0 entries left**. Before the change the same check left `tmp*` profiles.
- Pane-length TMPDIR: test_board_view, test_remote_push, test_web_screen, test_web_meet_code and test_remote_browser_storage all OK, 0 left, 0 `/dev/shm/chrome-*` left.
