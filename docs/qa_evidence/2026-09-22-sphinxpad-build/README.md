# Sphinxpad development build — #SPB2

Built 2026-09-22 from `fff7eb8fdf4617e2cc845805c209f32e264dd77d`.
Fetched origin: main ahead 121, behind 0; no other branches or worktrees.
No application code was uncommitted. Shared board edits and scratch/evidence files were preserved.
478 application/build source files were SHA-256 checked against the git revision after transfer.

Artifact on sphinxpad:
`/home/elliott/relay-debs/relay_0.1.0~gitfff7eb8f-1~ubuntu26.04_amd64.deb`

SHA-256: `af2d9b011f596b00510c8785145c252f967e4389e8c18414006e2e335229fc2c`

Build ID: `2026-09-22.14H.02`. Native Ubuntu 26.04 amd64, Qt6, pinned Ghostty core.
Source/build/logs remain under `sphinxpad:/home/elliott/relay-build/20260922-fff7eb8f/`.
Existing installed Relay was not replaced.

The clean native build used `packaging/deb/build-deb.sh` with `RELAY_SKIP_DEPS=1`,
`RELAY_JOBS=6`, a fresh `RELAY_BUILD_DIR`, and `RELAY_CACHE_DIR`.
CMake initially chose private Python 3.12; that test run was interrupted and CMake was
reconfigured with `-DPython3_EXECUTABLE=/usr/bin/python3` (3.14.4), then rebuilt.

## Validation

- `build-system-python.log`: full compilation succeeded; 87 of the first 89 CTest targets
  passed. `boardworkspace` and `boardexecute` failed twice. The backend suite timed out
  after 600.23 seconds with four earlier FAIL results; its automatic retry was stopped.
- `engine-test.log`: separate engine run failed two Ghostty cases:
  `CoreTest::wrappedUserRolesSurviveReflow` and `ViewTest::findCountsRewrappedMatchesOnce`.
- `package.log`: explicit `cpack -G DEB` succeeded after recording the failed test gate.
- `smoke.py` / `smoke.log`: extracted package metadata, core presence, version/help,
  worker ready/shutdown, and offscreen GUI with its own packaged worker and Bash integration
  all passed. Only the smoke process and its descendants were terminated.
- This is a development artifact, not a release that passed the full package gate.
  Failures recorded in #SBT2; #3BPH already tracks boardexecute and #99T0 records related
  backend timeout history.
- Board validation reports pre-existing repository errors (12 errors and 754 warnings on
  the initial check), unrelated to these new cards.
