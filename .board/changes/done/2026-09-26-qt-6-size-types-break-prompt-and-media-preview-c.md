---
id: EYT3
type: work
status: done
labels: [bug, qt6]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
resolution: done
discovered_from: 9Y7X
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [5e58c8fe3fba], evidence: [], related: [9Y7X, WV4V], github: null}
---
# Qt 6 size types break Relay compilation

## Issue
A full Qt 6 build found mixed `int`/`qsizetype` expressions in `ScreenPrompt.cpp`, `TerminalView.cpp`, `PaneChrome.h`, and `RelayWindow.h`. Convert bounded collection sizes to `int` at these UI interfaces so both Qt 5 and Qt 6 compile.

## Done means
A full Qt 6 build compiles `relay` without prompt or media preview size-type errors; the existing Qt 5 build retains the same behavior.

## Tests
manual: /home/elliott/repos/relay-terminal/src/ScreenPrompt.cpp

### Check PASS
`land.py try` built exact commit 5e58c8fe3fba with `-DRELAY_QT_MAJOR=6` and `-DRELAY_QT_MAJOR=5`, both targeting `relay`; both completed successfully.

## Execution Summary
Commit `5e58c8fe3fba` fixes the six mixed-size expressions found while validating the PDF package. The full `relay` target builds under both Qt 5 and Qt 6.
