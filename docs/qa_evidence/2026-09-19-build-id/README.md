# The build id in Options (2026-09-19)

Owner: "where does relay say what build it is? put that in settings", then "lets put the date and
then .01, .02, etc", then "do 2026-09-10.14H.01 (where XXH is the 24-H time)".

`scripts/build-id.py` numbers every relink of the app as `<date>.<hour>H.<count>`, counting from
.01 again each hour, and writes it beside the binary. Options › General › Diagnostics shows the
build this process is running.

- `options-diagnostics-build.png` — the row: build, version, when the process started, its path.
- `options-newer-build-on-disk.png` — the same row after the binary on disk was renumbered while
  Relay ran: it names the newer build and says how to get it (quit and reopen, or launch from the
  taskbar; "New window" stays in this process).

Both from the app under Xvfb. Rules: `docs/ARCHITECTURE.md` § Diagnostics.
