---
id: C8SV
type: work
status: inbox
labels: [bug, gui]
assignee: ''
rank: m
created: '2026-09-23'
source: 'Crash log investigation during #N6R8, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [N6R8], github: null}
---
# SIGSEGV immediately after a Globals suggestion acceptance event

## Issue
The live Relay GUI, build 2026-09-23.11H.02, logged `gui_crash signal=11` at 2026-09-23 16:44:18.932 UTC, PID 637025. The preceding event at 16:44:18.931 UTC was `globals_suggestion_accepted` on pane `2e7406f6`. The three crash frames are the signal handler, `__kernel_rt_sigreturn`, and an unmapped program counter (`0xb1f51ab28a00`); they do not identify the failing application function. Investigate the crash with a reproducible acceptance sequence or a debug run. The build that crashed has since been replaced, and there is no core file.
