# Verify #7BM4 — independent verification record (Kimi K3, 2026-09-22)

Checked revision: `0241d05ef193` (named by the driving session; this archive has no `.git`, so
the hash cannot be confirmed from inside — recorded as a limitation). Binary: `build/relay` as
shipped in the archive. Fixture: `/tmp/verify-7bm4-fresh/orders`, already staged; restage with
`stage.sh` (calls the implementer's `scenario/stage.py --fresh`, then replays every step with
`drive.sh`). Screens read with tesseract OCR; every claim below is also cross-checked on disk
(card file, history JSONL, profile evidence files) where the screen cannot show it.

## Tests run (the card's `## Tests` lines)

- `ctest -R testsuites` — passed (0.17 s)
- `ctest -R cardtests` — passed (0.21 s)
- `ctest -R profilepane` — passed (0.03 s)
- `ctest -R windowstate` — passed (0.08 s)
- `tests/test_test_probe.py`, `tests/test_test_history.py`, `tests/test_junit_runner.py`,
  `tests/test_tests_protocol.py`, `tests/test_profile_protocol.py`, `tests/test_relay_profile.py`
  — 237 tests, all pass (`PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest …`, 7.9 s)
- `manual: HUMAN-QA.md` — the mechanical (actor `both`) steps of that walkthrough are what this
  directory replays; the human-only steps and the three questions stay with the owner.

## Simulation steps played (scenario.json, actor `both`)

| Step | Seen | Shot |
|---|---|---|
| 1.1 open the SYCG card | strip: "Tests 4 listed · never checked", Check button; card body lists the 4 tests | `discover-10-card.png` |
| 1.2 press Check | "1 passed · 1 failed · 1 missing evidence · 1 not applicable · 3 findings, revision a1efba101a74"; rounding not in the project any more, totals_large_order failed last run, test_invoice 3/3 never ran; actions Run these / Replace retired check / Open the failing one; dated `### Check 2026-09-21 21:28` block written into the card file (verified on disk) | `check-11-checked.png` |
| 1.3 move to Done | refused: "#SYCG is not proven yet: 1 failed, 1 missing-evidence (totals_large_order, test_invoice). Run them, use an existing result, or move it with an override that says why. Override…"; status on disk stayed `needs-verification`. Note: the notice names failed+missing only; the gone test (rounding) is surfaced in the findings list with a "Replace retired check" action, not in the notice | `gate-12-status-open.png` |
| 2.1 press Tests | Test suites pane: "7 tests · 3 passed (1 slow, 1 flaky) · 1 failed · 3 never run"; first row inventory_sync, flaky, 70% | `tests-20-pane.png` |
| 2.2 click inventory_sync | "70% reliable over 20 runs · flake score 5.3 · no card names this test · Last failure yesterday · a1efba10". The failure message and per-host history are below the fold in the default split (implementer finding 2, a Human QA point); the data itself confirmed in `board/.private/tests/history.jsonl`: pass+fail at commit 3f2a9c1e on desktop, message "FAIL: warehouse did not answer within 200 ms", 20 runs across desktop+laptop | `detail-21-detail.png` |
| 3.1 Profile menu | four targets: Build (this machine) / Build (another machine) / Python tests / The app, each with a one-line explanation | `profile-30-menu.png` |
| 3.2 Profile → Build (this machine) | result pane "8 steps · 2.7 s wall · 2.8 s of compile time · spark-dcc9", table names `src/report.cpp.o` 2.7 s at 94.0 % share, Open flame graph + Attach to card…; evidence files (summary.md, ninja_log, build.trace.json, …) verified on disk under the fixture's `docs/qa_evidence/2026-09-21-profile-build/` | `profilerun-31-result.png` |

## Notes and limitations

- The implementer's `ai-pass.sh` default coordinates missed on this fixture (fewer expanded
  sections shift every row up); `drive.sh` uses OCR-discovered coordinates. Same brittleness the
  implementer recorded as finding 4.
- `scripts/relay-speedscope --print-url` was not exercisable here: the speedscope bundle is not
  installed in this disposable profile (the script says so and names `relay-tooling-setup
  --install`). Environment, not code; the flame-graph path is otherwise visible in the result
  pane and summary.md.
- Docs present: `docs/PROFILING.md`, `docs/SWITCHBOARD-TOOLING-RESEARCH.md`,
  `docs/SWITCHBOARD-DESIGN.md` §4.14, `docs/SWITCHBOARD-FORMAT.md` §2.6,
  `docs/AGENT-SESSIONS-PROTOCOL.md` §31.9, `docs/VALIDATION.md` points at the pane.
- The card has **no `## Done means` section** — recorded as a finding per the brief.
- `## Human QA` questions left unanswered, as instructed.
