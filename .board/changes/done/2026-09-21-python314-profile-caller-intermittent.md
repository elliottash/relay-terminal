---
id: PF14
type: work
status: done
labels: [bug, tests, profiling]
assignee: codex
session: windows-platform-profile
rank: mpf14
created: '2026-09-21'
source: 'Measured by Codex during release verification for #R6BS'
links: {plans: [], commits: [], evidence: ['https://github.com/elliottash/relay-terminal/actions/runs/35667599551'], related: [R6BS, 7BM4], github: null}
---
# Profile roundtrip intermittently loses the expected caller total

## Issue
Measured during #R6BS release verification: Ubuntu 26.04 arm64, Python 3.14,
`test_relay_profile.ConverterTests.test_pstats_round_trips_through_speedscope` failed on the
backend test retry, with `middle.total = 0.000005` and `leaf.self = 0.005258`, violating
`middle.total >= leaf.self`. The first backend pass in the same job passed this test.

## Discussion points
The converter chooses each function's heaviest recorded caller. The test uses a live cProfile
capture, then expects the leaf's sample to contribute to its known caller's total. The failing
profile itself was not retained, so it is not known whether the capture omitted an edge, the
converter chose an unexpected edge, or the fixture's flattened names concealed another frame.
Do not weaken the caller assertion or mark this resolved without identifying the failed input.

## Planning notes
The failure is in run 35667599551, job 106556877075, artifact `deb-5`,
`out/logs/build-ubuntu-26-04.log`, lines 15824–15836. The other five Linux jobs had no profile
failure. Locally, a matching Ubuntu 26.04 arm64 container with Python 3.14 passed all ten converter
tests, 500 direct profile conversions, and 1500 invocations of the exact failing unittest method.
Those repetitions captured the raw pstats data on failure, but none failed. Diagnostic scripts
and results are `/tmp/reproduce_profile.py`, `/tmp/reproduce_profile_case.py`,
`/tmp/profile-repro.log`, and `/tmp/profile-repro-case.log` on the investigating machine.
The same failure recurred in release run 35669387286, Debian 13 arm64 (Python 3.13), artifact
`deb-3`, `out/logs/build-debian-trixie.log`, lines 9889–9895: `middle.total = 0.000006` versus
`leaf.self = 0.005672`. It occurred on the first backend pass; the retry passed. This rules out a
Python 3.14-only explanation. The assertion now includes raw pstats and the converted speedscope
document on failure so the next occurrence preserves the input graph. Its condition is unchanged,
and no product code was changed. A subsequent release must retain the assertion and pass its
gates; those passes would be release evidence, not proof that this intermittent issue is fixed.

## Tests
`PYTHONPATH=backend:tests python3 -m unittest test_relay_profile.ConverterTests -v`

## Done means
The live cProfile fixture records only its intended workload even while other suite threads
run; the original caller-total assertion remains and passes under the measured interference.

## Plan
Run the generated leaf/middle workload in a fresh Python subprocess, then convert and assert
the real profile in the parent exactly as before. Retain a concurrent idle-server reproducer
and the failing raw statistics; verify it before and after without changing converter behavior.

## Execution Summary
Reproduced the same corrupt caller graph with eight idle socketserver threads and the existing
roundtrip test on Python 3.12.3, without the full suite. cProfile receives interpreter-wide
monitoring callbacks and mixes these threads into its shared caller context. The fixture now
captures its generated workload in a fresh Python process; conversion and the original caller
assertion still run unchanged. No converter/product behavior was modified.

## Resolution
The test fixture is isolated from other suite threads. Ten converter tests and 100 repetitions
under the measured idle-server interference pass. Evidence, reproduction, and primary CPython
references: `docs/qa_evidence/2026-09-22-profile-caller-ubuntu24-arm64/isolation.md`.
This does not claim to repair CPython's arbitrary multithreaded profile captures.
