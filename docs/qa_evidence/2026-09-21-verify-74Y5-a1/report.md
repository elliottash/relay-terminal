# Independent verification of #74Y5

2026-09-21 local / 2026-09-22 UTC, Codex verifier a1. **PASS** for the requested naming seam and QA instructions. No implementation changes.

The numbered path was written before staging in `plan.md`: check 3, agent 3, person 0, human minutes 0. This is mechanical verification, with no claim to have measured human usability.

## Criteria and results

| Done means | Verdict | Evidence |
|---|---|---|
| Verify and Try it briefs explicitly conserve human time and state minutes | PASS | `src/BoardModel.cpp:666` generated Verify brief and `backend/relay_core/board_tryit_brief.md:26` carry the objective and minute estimate instruction. |
| Numbered path before staging, check/agent/person labels and counts, reason for every person step | PASS | Both briefs require this; this run followed it in `plan.md`. No person step was needed, so no human judgement was fabricated. |
| Open card, press named control, read notice/rendered sections, type named box; scenario rewritten without coordinates | PASS | `drive.py` performed 19 socket operations in the actual UI. `transcript.json` proves card T9QA loaded the unique marker, Check produced a missing-test finding, Board filter changed to T9QA and was read back, then card reopened. Missing button, hidden filter and composer typing all returned explicit refusal. Read notice returned the actual empty line. `ui.png` visually confirms the Check result. Scenario ai-pass.sh now calls relay-drive open/press/read/action/panes and contains no xdotool/coordinate interaction; it was reviewed, not rerun in all three phases. |
| One-page design rule and Verify pointer | PASS | `docs/DRIVING-APPS.md` specifies names, open/press/read, explicit refusals, disposable fixtures and asynchronous assertion polling; Verify points to it at BoardModel.cpp:672. |

Targeted checks: built through `RELAY_SESSION=verify-74y5-a1 scripts/relay-build --wait-seconds 10 --target relay-board-tests relay-boardpane-tests relay-appcommands-tests`, then `ctest --test-dir build -R '^(board|boardpane|appcommands)$' --output-on-failure`: 3/3 PASS (`tests.txt`). These unit binaries are from the shared checkout build; live acceptance evidence instead uses the exact clean gate described below. Gate did not contain those test executables initially. Board test includes Verify wording assertions; boardpane covers named controls/refusals; appcommands covers registry behavior.

## Tested runtime

Clean gate `/tmp/claude-1000/land/driven/verify/build/relay`, SHA256 `637ec11d705dce90a6c6598e5ab2c1c31b3c2cb046f72181ea99a290bd8f9bae`; matching backend/data `/tmp/claude-1000/land/driven/verify/src`. Compared 391 manifest entries under src/, engine/, backend/, shell/, CMakeLists.txt and scripts/relay-drive against `ece752ef33a41f3cb2fc1b3ee90a89c9c67863c5`: zero mismatches. This is the live tested revision, not a claim about subsequent main changes.

Fresh disposable board and isolated Xvfb at `:300`, sandbox `/tmp/v74y5-uf8jdy6h`. HOME/config/data/cache/runtime/tmp isolated, keyring off, no model prompt/credentials. `RELAY_NO_ISOLATION=1` disables per-pane memory cgroups inside this UI fixture; filesystem/profile isolation remains as recorded in drive.py. App and Xvfb stopped after capture. No screen-coordinate input used.

No remaining failure observed. The broken test reference in the fixture is deliberate seeded input; Check correctly reports it not applicable, proving its result was read rather than accepting the button click as completion. A successful driver operation can begin async work, so consumers must poll for resulting content. This run asserts the rendered marker and resultant findings.
