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

## Findings
- Sampling rides `RelayWindow::refreshPaneStatus()` (the existing 400 ms poll that resolves pane states), so every pane is measured over the same interval and no new timer exists.
- `/proc` parsing: `stat` fields 14+15 (utime+stime) counted after the last `)` because comm may contain spaces; `statm` field 2 for resident pages; tree walk over `/proc/<pid>/task/<pid>/children`, capped at 256 processes — the same walk `Pane::programWaitingForInput()` already does.
- The pane's shell is always `/bin/bash --rcfile <data>/shell/integration.bash` (src/Pane.h, `startShell`), so the meter's roots are `shellPid()` and the worker's `processId()` while it runs.
- The three labels share one source (`relay::usage`): pane chip `20% 0%` with painted glyphs, tab suffix `· 20% / 0%`, Sessions tag `cpu 20% · mem 0%`.
- Under bare Xvfb there is no window manager, so GUI smoke needs `xdotool windowfocus` (not `windowactivate`), the instruction-files dialog dismissed via its "Not now" button, and Ctrl+H to hand the keyboard to the terminal before typing.

## QA checklist
- [ ] `./build/relay-paneusage-tests` and `./build/relay-conversations-tests` pass (12 + 31 tests; the latter includes `liveUsageTagsComeAndGo`).
- [ ] A busy pane (e.g. `for i in 1 2 3; do yes >/dev/null & done; sleep 60`) shows the `X% Y%` chip in its header right of the state word, and the tab label ends `· X% / Y%`; the tooltip on each spells out CPU vs memory (`docs/qa_evidence/2026-09-19-pane-cpu-ram-meters/`).
- [ ] An idle pane shows no chip and no tab suffix — the header looks exactly as before.
- [ ] The numbers move as load changes (kill the `yes` processes: CPU falls to 0 within ~a second, then the chip and suffix disappear).
- [ ] Options → Appearance → "Pane CPU and memory" off removes them everywhere, on re-appears within one poll (~0.5 s).
- [ ] With an agent configured, the Sessions pane (/resume) shows `cpu X% · mem Y%` beside "open" on that conversation's row while its pane is busy, and the tag goes when it goes idle.
- [ ] A remote (ssh) pane shows the local ssh client's usage and its chip tooltip says the far machine is not measured.
- [ ] `ctest --test-dir build` passes (54 targets; `backend-and-bash` has been flaky in the shared tree this evening — see the card thread).
