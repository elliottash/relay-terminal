---
id: 6DQ8
type: work
status: needs-qa-llm
labels: [feature, isolation, settings]
implemented_by: claude-opus-4-5
rank: zzzzj
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [fabdba2], evidence: [], related: [], github: null}
---
# Calibrate isolation memory limits to system RAM and expose them in Options

## Issue
should that be calibrated to system resources and set in the options?

## Context
Both agent workers OOM'd on 2026-09-19 around 00:05 and 00:10 while their agents ran
C++ builds (`cc1plus`, killed at a 2.0G peak + 512M swap — exactly the `agent_memory_max`
default). The machine has 121G RAM. The limits are configurable only via `[isolation]` in
`relay.conf` (`agent_memory_max`, `agent_swap_max`, `shell_memory_max`, …), validated by
`^(\d+[KMGT]?|infinity)$`, with no Options page and no awareness of machine size.

## Proposal
- Defaults derived once from `/proc/meminfo` `MemTotal`: agent = clamp(RAM/16, 2G, 8G),
  shell = clamp(RAM/2, 4G, 16G), swap caps proportional (agent min(2G, SwapTotal/4)).
  On this 121G machine: agent 8G, shell 16G.
- A small Options page ("Isolation" or rows on an existing page) via the catalog-driven
  `SettingsSection`/`SettingRow` framework in `SettingsPane.cpp` + `RelayWindow.h settingsSections()`:
  per-pane memory limit with an "Auto (8G on this machine)" default and explicit override, writing
  the existing `isolation/*` QSettings keys so `relay.conf` stays the backing store and manual
  `[isolation]` edits keep working. The page gets the reset-to-defaults row for free.
- `Isolation.h memory()` gains the computed fallback; the systemd `MemoryMax=` plumbing is unchanged.

## Resolution (found already implemented by the 2026-09-19 board sweep)

Both halves of the proposal are on `main` (`fabdba2`), and the card had simply never left
`in-progress` / `waiting_on: owner`.

- **Calibrated defaults**, `src/Isolation.h`: `memInfo()` reads `/proc/meminfo`, and
  `agentDefault()` = clamp(RAM/16, 2G, 8G), `shellDefault()` = clamp(RAM/2, 4G, 16G),
  `agentSwapDefault()` = clamp(Swap/8, 512M, 2G), `shellSwapDefault()` = clamp(Swap/4, 1G, 4G) —
  the formulas this card proposed. An unreadable `/proc/meminfo` falls back to the old flat values.
- **Options rows**, `RelayWindow::settingsSections()`: the agent and shell memory caps each get a
  row with an "Auto — %1 on this machine" choice that removes the key, and explicit overrides that
  write the same `isolation/*` QSettings keys, so `relay.conf` stays the backing store.

The open question on the thread ("should `run_command` children share the worker's cap?") was
answered by the implementation: they stay in the worker's scope, with the cap rising with RAM.

## QA checklist
- [ ] On this 121G machine the Options rows read "Auto — 8G" (agent) and "Auto — 16G" (shell), and `systemd-run` receives those values.
- [ ] Setting an explicit override writes `isolation/agent_memory_max` and the scope picks it up on the next pane.
- [ ] Choosing Auto again removes the key, and a hand-edited `[isolation]` in `relay.conf` still wins where no Options value is set.
- [ ] A build that OOM'd at the old 2G default now completes in an agent pane.
