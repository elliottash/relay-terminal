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

Not done: a sweep of directories left by a crash or SIGKILL. It needs an owner mark in each
directory (a pid file) to tell a dead Relay's from a live one's; worth a card of its own.

Files: `src/main.cpp`. No protocol change.

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
