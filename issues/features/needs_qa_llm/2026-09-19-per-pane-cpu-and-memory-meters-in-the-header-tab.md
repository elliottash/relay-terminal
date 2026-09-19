---
id: D03W
type: work
status: needs-qa-llm
labels: [feature, terminal, gui]
implemented_by: glm-5.3
rank: zzzzzzr
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-pane-cpu-ram-meters/], related: [], github: null}
---
# Per-pane CPU and memory meters in the header, tab and session manager

## Issue
would it be possible to have small X%, X% indicators for CPU and RAM usage by pane and tab? investigate and try adding it

## Decisions
- **What a pane's usage counts** (agent, from the investigation): the pane's two local process trees — the shell its pty spawned with whatever it is running, and the pane's agent worker — not Relay's own engine threads, which cannot be attributed to one pane. A remote pane measures the local ssh client; the chip's tooltip says so.
- **CPU % is of the whole machine** (100 = every core), so a tab's panes sum honestly; memory % is of physical RAM.
- **An idle pane shows nothing**: the chip, the tab suffix and the Sessions tag appear only while the pane is using ≥ 0.5 % CPU or memory, so an idle terminal looks exactly as before.
- **The Sessions pane asked for too** (owner, 2026-09-19: "show the %cpu/%mem in the session manager as well"): a labelled `cpu 12% · mem 3%` tag beside "open", since a bare `12% / 3%` would sit next to words.
- `appearance/pane_usage` (Options → Appearance, default on) turns the meters off.
- **Fix the walk, and break the number down** (owner, 2026-09-19, on the review below: "fix that,
  and add CPU/MEM% to children"): the walk follows every thread's children, there is one walk
  function for both readers, and the two tooltips name the busiest processes behind the sum. The
  chip and the tab label stay the sum alone.

## Findings
- Sampling rides `RelayWindow::refreshPaneStatus()` (the existing 400 ms poll that resolves pane states), so every pane is measured over the same interval and no new timer exists.
- `/proc` parsing: `stat` fields 14+15 (utime+stime) counted after the last `)` because comm may contain spaces; `statm` field 2 for resident pages; tree walk capped at 256 processes — over the same tree `Pane::programWaitingForInput()` walks, and since the review below, literally the same function.
- **A walk of `/proc/<pid>/task/<pid>/children` misses half the tree** (found on review, fixed
  2026-09-19): that file is the *main thread's* children. A process forked from any other thread
  is parented to that thread, which is exactly what the Python worker does when it spawns a
  subprocess off a worker thread — so the pane's biggest child could be invisible until the worker
  reaped it and its ticks turned up in `cutime`. `relay::usage::walkTrees()` now reads
  `/proc/<pid>/task/<tid>/children` for every `<tid>` in `/proc/<pid>/task`.
- **One walk function, two readers**: `Pane::programWaitingForInput()` had a second, hand-rolled
  walk over the same shell pid on its own 250 ms poll. It goes through `walkTrees()` now, with
  `Detail::PidsOnly` (no `stat`, no `statm` — that poll is four times a second and only wants the
  shape of the tree) and its own cap of 64. Its decision logic and cadence are unchanged.
- **Per-process CPU and memory**: each process's own tick delta between two polls, keyed by pid
  *and* `starttime` so a recycled pid is a new process rather than a lifetime of ticks in one
  interval. Sorted by CPU then memory, the busiest five, rows that round to 0 % on both axes
  dropped. `relay::usage::setProcRoot()` makes the walk testable: the tests build a `/proc` out of
  directories, a process under a non-main thread included.
- The pane's shell is always `/bin/bash --rcfile <data>/shell/integration.bash` (src/Pane.h, `startShell`), so the meter's roots are `shellPid()` and the worker's `processId()` while it runs.
- The three labels share one source (`relay::usage`): pane chip `20% 0%` with painted glyphs, tab suffix `· 20% / 0%`, Sessions tag `cpu 20% · mem 0%`.
- Under bare Xvfb there is no window manager, so GUI smoke needs `xdotool windowfocus` (not `windowactivate`), the instruction-files dialog dismissed via its "Not now" button, and Ctrl+H to hand the keyboard to the terminal before typing.

## QA checklist
- [ ] `./build/relay-paneusage-tests` and `./build/relay-conversations-tests` pass (19 + 31 tests; the latter includes `liveUsageTagsComeAndGo`).
- [ ] A busy pane (e.g. `for i in 1 2 3; do yes >/dev/null & done; sleep 60`) shows the `X% Y%` chip in its header right of the state word, and the tab label ends `· X% / Y%`; the tooltip on each spells out CPU vs memory (`docs/qa_evidence/2026-09-19-pane-cpu-ram-meters/`).
- [ ] The chip's tooltip and the tab tooltip's usage section list the busiest processes behind that
      number, one `<name> · X% cpu · Y% mem` per line, with the pane's own two roots reading
      `shell` and `agent worker`; the chip itself still shows only the sum. Killing one `yes`
      drops its line within a poll.
- [ ] A pane whose agent is running a tool that spawns a subprocess (the worker does this from a
      worker thread) counts that subprocess: the chip moves while it runs, not only when it ends.
- [ ] An idle pane shows no chip and no tab suffix — the header looks exactly as before.
- [ ] The numbers move as load changes (kill the `yes` processes: CPU falls to 0 within ~a second, then the chip and suffix disappear).
- [ ] Options → Appearance → "Pane CPU and memory" off removes them everywhere, on re-appears within one poll (~0.5 s).
- [ ] With an agent configured, the Sessions pane (/resume) shows `cpu X% · mem Y%` beside "open" on that conversation's row while its pane is busy, and the tag goes when it goes idle.
- [ ] A remote (ssh) pane shows the local ssh client's usage and its chip tooltip says the far machine is not measured.
- [ ] `ctest --test-dir build` passes (54 targets; `backend-and-bash` has been flaky in the shared tree this evening — see the card thread).
