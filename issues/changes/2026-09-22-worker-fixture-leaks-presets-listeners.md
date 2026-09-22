---
id: LSP1
type: work
status: inbox
labels: [bug, tests]
assignee: null
rank: zlsp1
created: '2026-09-22'
source: 'Measured by Codex during #R6BS release verification'
links: {plans: [], commits: [], evidence: [], related: [R6BS], github: null}
---
# In-process worker fixture leaks presets callbacks into later stdout captures

## Issue
Release candidate 82, Ubuntu 26.04 amd64, failed
`test_guest_hook.PermissionDecision.test_an_answer_for_another_question_is_not_read`:
expected `(0, '')`, got `(0, '{"event": "presets", ...}')` with a 156950-character payload.
The permission decision itself was not emitted. The other five Linux jobs passed.

## Discussion points
`WorkerProtocolTests.run_worker` in `tests/test_guest_harness_provider.py` runs the real
worker loop in the test interpreter. `worker.main` registers four global catalog listeners,
but the fixture cleans up only the guest-harness listener. OpenRouter, custom-provider and
Relay Pro callbacks remain registered. `emit_presets` starts an unrelated real OpenRouter
background fetch; its completion invokes a worker closure whose `emit` reads the current
process-wide `sys.stdout`. The guest-hook fixture temporarily replaces exactly that stream.

A bounded deterministic reproduction ran the existing worker test, confirmed the OpenRouter
listener survived cleanup, then invoked that same callback from a timer while the unchanged
permission test captured stdout. It failed with the same presets payload in 0.38 seconds on
local Python 3.12.3. Network refreshes were disabled in this reproducer; triggering the leaked
callback explicitly proves the capture mechanism without relying on network timing.

## Planning notes
Keep the guest-hook assertion and production permission behavior unchanged. Scope and restore
all listeners in the in-process worker fixture; disable unrelated live catalog refreshes there.
A regression should verify no callback survives fixture teardown or contaminates later output.
Do not delay the current release on speculation: candidate 7d is still being verified separately.

## Tests
Evidence log: `/tmp/relay-linux-82-ubuntu26-evidence/out/logs/build-ubuntu-26-04.log`, line 15952.
Reproducer: `/tmp/relay-hook-presets-repro.py`; result: `/tmp/relay-hook-presets-repro.log`.
No source or test fix has been applied by this investigation.
