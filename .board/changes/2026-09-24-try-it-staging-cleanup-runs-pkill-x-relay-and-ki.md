---
id: 8YNJ
type: work
status: inbox
labels: [bug, try-it]
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
source: pane 1, 2026-09-24 — traced from relay.log / worker.log / conversations DB after the app closed itself at 18:24 local
links: {plans: [], commits: [], evidence: [], related: [Z82M, 9JYK], github: null}
---
# Try-it staging cleanup runs `pkill -x relay` and kills the live desktop app

## Issue
why did it close a few minutes ago

`#Z82M` Try-it staging (agent pane `c5d9948d`, session `bb3324c2…`) launches a sandboxed Relay
under Xvfb (`DISPLAY=:42`, binary `/tmp/z82m-verify/build/relay`) and cleans up with `pkill -x
relay`. `-x relay` matches **every** process whose basename is `relay` — including the live desktop
app (`build/relay`) — so the cleanup takes the owner's session down with the sandbox.

Measured evidence (2026-09-24, `~/.local/share/relay/logs/` + conversations DB):

- 22:24:47.548Z `tool_started run_command` pane `c5d9948d` — conversations entry id 257820:
  `fuser -k 4761/tcp; fuser -k 4762/tcp; pkill -x relay 2>/dev/null; pkill -x Xvfb; sleep 2; …`
- 22:24:47.580Z `gui_quit reason=signal pid=2484272 uptime_s=2012 build=2026-09-24.17H.04` — the
  SIGTERM hit the live app 32 ms into that command. Graceful shutdown followed (every pane's
  `worker_exit reason=shutdown expected=1`); the current instance started 22:24:55Z (18H.04).
- The same pane ran the same cleanup at 21:50:59Z (entry id 252269), which ended the 21:47:54Z
  instance. It will recur with every Try-it verification round.

Not a crash: no `gui_crash` line, and #9JYK's signal-quit path did its job — layout and
scrollbacks saved; the pane's own tool call completed `ok=True` two seconds later (worker.log).

Fix that belongs in the staging procedure: never `pkill -x relay`. Target the sandbox by exact pid
file (`$(cat …/z82m/relay.pid)`), by full path (`pkill -f 'z82m-verify/build/relay'`), or via its
private `XDG_RUNTIME_DIR` — and the Try-it guidance should say so, so later cards do not inherit
the pattern.
