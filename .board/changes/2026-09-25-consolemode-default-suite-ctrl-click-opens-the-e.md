---
id: DJ3X
type: work
status: discussing
labels: [bug, tests, consolemode]
discovered_from: 8ABD
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# consolemode default suite: Ctrl+click opens the editor with the wrong line (pre-existing on main)

## Issue
The console-mode default suite fails on plain main (verified in a clean `git archive main` export, no card #8ABD changes): `ctrlClickEditsTheActualFile` expects Ctrl+click on an output-target line to edit the file at line 2, but `onEditPath` fires with the wrong line (or a second call overwrites it), and a directory click lands on the context with a different payload. Reproduces every run in both the shared checkout and a clean tip export, so the regression is in already-landed code, not in #8ABD's in-flight edits.

## Evidence
2026-09-25, verified on a clean `git archive main` export built and run fresh: the default (no-arg) `relay-consolemode-tests` suite fails 5 checks, in two unrelated areas, both reproducible every run:

1. `ctrlClickEditsTheActualFile` — `edited != path`, `line != 2`, `context.seen != QStringList({home->path()})` (tests/consolemode_test.cpp:720-733 on main).
2. Turn-summary spacing — `text.contains("▸ ran pytest\n\nThe tests pass.")` and `text.contains("▸ ran ctest\n✦ 2 tool calls · 2 s")` fail (tests/consolemode_test.cpp:2004/2007 on main).

All `--X-only` filter suites pass on the same tree, so the two areas only surface in the default suite's longer run. Card #8ABD's in-flight edits add zero failures (same 5 on tip + #8ABD's files).
