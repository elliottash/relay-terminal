---
id: 9JYK
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code, milestone review), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: '`kill <relay pid>` exits 0, leaves no /tmp/relay-* directory behind, and writes windows.json and the panes'' scrollback; a plain quit removes the panes'' directories too; `ctest` (29) passes'
source: 'milestone review of the code base, 2026-09-18: about 870 stale /tmp/relay-* directories on the owner''s machine with one Relay running'
links: {plans: [], commits: [0f49c89], evidence: ['docs/qa_evidence/2026-09-18-a-signal-is-a-quit/'], related: [SB7K], github: null}
---
# A signal is a quit: SIGTERM saves, and a quit cleans up after its panes

## Report

`/tmp` held about 490 `relay-XXXXXX` and 386 `relay-open-XXXXXX` directories (876 in all, 44 MB)
while one Relay was running, which was using four of the first and one of the second. (The commit
message of 0f49c89 says 1,262: that figure counted the `relay-open-*` directories twice.) Each `relay-XXXXXX` is a pane's private runtime directory and keeps its last
`state.json` — the shell's `PATH` and the names of its aliases and functions.

Two causes, one behind the other:

1. **Nothing handled SIGTERM.** The comment in `main()` said a quit by "SIGTERM through Qt" still
   saves; Qt installs no such handler. `kill`, `pkill relay`, a systemd stop, a logout without a
   session manager — and every QA driver in `docs/qa_evidence/` — ended Relay where it stood:
   exit 143, no scrollback saved (#SB7K only writes it on the way out), workers not told to shut
   down, temporary directories left.
2. **A quit never destroyed the panes.** Windows are heap objects with `WA_DeleteOnClose`, held
   by `QPointer`, so only *closing* one deletes it. A quit stops the event loop with them open,
   and `~Pane` — which shuts the worker down and removes the directory — never ran. With the
   handler alone, the socket directory went and the pane's stayed.

## Change

- `installQuitSignals()`: SIGTERM, SIGINT and SIGHUP write one byte to a pipe (the only thing a
  handler may safely do); a `QSocketNotifier` turns it into `QCoreApplication::quit()`, so
  `aboutToQuit` saves the layout and the scrollback as it does for any quit. A signal Relay was
  started ignoring (`nohup`) stays ignored. Pane shells are unaffected: the PTY child resets
  every signal before `exec` (`engine/pty/PtyUnix.cpp`).
- `WindowManager::~WindowManager()` deletes the windows a quit left open.

- **The sweep for what a crash leaves.** A quit is now clean, but `kill -KILL`, an OOM kill and a
  segfault still leak a directory, so each one carries an owner file: `owner` (0600, written
  atomically the moment the directory is made) holding the pid *and* that process's `starttime`
  from `/proc/<pid>/stat` — pids are recycled, and a bare pid would spare a stranger's directory
  for ever. Three seconds after startup, `relay::runtimedirs::sweep()` (src/RuntimeDirs.h) walks
  `$TMPDIR`: an owner that is gone → removed; an owner still running → kept, so a second Relay
  never touches the first's; no owner file at all (an older build's, or a mark a millisecond from
  being written) → kept for a week, because an older build may still be running with an idle pane. It looks only at names `QTemporaryDir` itself could have made
  (`relay-XXXXXX`, `relay-open-XXXXXX`), only at real directories that are not symlinks, owned by
  this uid, mode 0700, canonically inside `$TMPDIR` — `/tmp` also holds `relay-qa-*` and
  `relay-*-shots` that are none of Relay's business. It never follows a symlink while removing, and
  stops after 400 directories or 1.5 s so a `/tmp` with thousands of leftovers cannot slow a start.
  One log line, `runtime_sweep removed=… kept_alive=… kept_young=… errors=…`, and only when
  something was removed or failed.

Files: `src/main.cpp`, `src/RuntimeDirs.cpp/.h`, `tests/runtimedirs_test.cpp`, `CMakeLists.txt`.
No protocol change.

## QA checklist

1. **The report.** `docs/qa_evidence/2026-09-18-a-signal-is-a-quit/drive.sh build/relay <dir>`:
   exit status 0, "tmp dirs left=0", "layout saved: 1", "scrollback files: 1", empty stderr.
2. **Ctrl+C in the launching terminal** (SIGINT) and closing that terminal (SIGHUP) behave the
   same as 1.
3. **Restore after a kill.** Print something in a pane, `kill` Relay, start it: the pane comes
   back with its scrollback (#SB7K), exactly as after closing the window.
4. **Several panes and windows.** Two windows, splits in each, an agent turn running in one:
   `kill` exits within a few seconds, no `python3 … worker.py` of that Relay survives, no
   directory is left.
5. **Closing windows is unchanged.** Close the last window by its ×: same result, no crash on
   the way out (the destructor meets an empty list).
6. **Shells still get their signals.** In a pane, `sleep 100` then Ctrl+C interrupts it;
   `kill -TERM $$` from a pane ends that shell only.
7. **Every directory is marked.** With Relay running, each `$TMPDIR/relay-XXXXXX` and
   `$TMPDIR/relay-open-XXXXXX` holds an `owner` file, mode `-rw-------`, naming that Relay's pid
   and a non-zero `starttime`.
8. **SIGKILL, then restart.** `kill -KILL <relay pid>` leaves the directories behind; start Relay
   again and within ~5 s they are gone, the new run's are there instead, and `relay.log` has one
   `runtime_sweep removed=2 …` line. (Use an isolated `TMPDIR`; the sweep follows it.)
9. **Two Relays at once.** With one running, start a second on the same `TMPDIR`: after its sweep
   both sets of directories are still there — the second must never delete the first's, and
   neither may delete its own. Quit both: `TMPDIR` is empty.
10. **Bystanders.** A `relay-qa-something`, a `relay-abc`, a `relay-XXXXXX`-shaped directory that
    is mode 0755 or another user's, and a symlink named `relay-aaaaaa` pointing somewhere else are
    all untouched by a sweep, however old.
