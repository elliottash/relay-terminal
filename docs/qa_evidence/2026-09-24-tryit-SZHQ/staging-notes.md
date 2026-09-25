# Staging notes — #SZHQ Try it

Staged offline with `stage.sh` under `/tmp/claude-1000/tryit/szhq`: an invented
scratch root (`claude-1000/-home-user-demo`) holding three session trees — a
200 MB build idle 30 h, a 4 KB dead session idle 30 h, and a fresh 8 KB tree a
real `sleep` process sits in — with the budget forced to 0.1 GB so it is already
over. Ages are faked with `touch -d`; the "live process" is a real background
`sleep 600`, which exercises the same /proc cwd check a live agent pane would.
Sizes and layout mirror production; nothing outside the fixture is touched,
because `RELAY_SCRATCH_ROOTS` pins every command to the sandbox.

Not staged: the in-app bell notification (src/ScratchMonitor) — its verdict
parsing, once-a-day rule and action handling are covered by the `scratchmonitor`
and `notifications` ctest suites on the landed tree; judging the notification
copy would need an app run, which this kit deliberately avoids.

The agent ran the full path itself first (captures 00–04): check exits 1 with
the one line, the dry run lists the two idle trees and names the live one's
reason, `--apply` frees 200 MB in 2 entries, and `session-live/pane.h` survives.
