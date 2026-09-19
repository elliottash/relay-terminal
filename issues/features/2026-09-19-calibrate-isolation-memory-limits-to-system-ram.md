---
id: 6DQ8
type: work
status: in-progress
labels: [feature, isolation, settings]
implemented_by: claude-opus-4-5
waiting_on: owner
rank: zzzzj
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
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
