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

The owner identified the action immediately before the crash: "the crash happened when i clicked a link to 'keep' a memory". The link is the transcript's `Keep` link, which sends `globals_suggestion_accept` from `Pane::decideMemorySuggestion`. The logged accepted reply narrows the failure to GUI processing after the backend saved the memory. `Pane::handleMemorySuggestionEvent` writes the outcome line and refreshes any visible Globals panes; the available stack does not identify which operation failed.

## Tests
The memory and Globals protocol unit tests pass (21 tests). The `globalspane` CTest passes. The full `consolemode` CTest fails at its queued-prompt assertion, `tests/consolemode_test.cpp:1082`, before the memory acceptance case; that failure is unrelated to this crash investigation. A live or isolated GUI reproduction of the transcript link remains needed.
