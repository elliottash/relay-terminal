# CFG1 verification and follow-up fix — 2026-09-22 UTC

The post-fix live run passes. `post-fix/result.json` is produced only after all protocol assertions succeed; `post-fix/run.txt` records the completed driver.

The original run (files in this directory) caught a real Options propagation defect: the terminal received a step limit of 37 while the helper stayed at 500. Its failing trace and screenshots are preserved. `src/RelayWindow.h` now wraps the two Security turn-limit rows with the existing `alsoBoardWorkers()` helper, pushing both changes and resets to live helper workers. Only that +10/-2 source hunk belongs to this fix.

The fresh isolated Xvfb/HOME/XDG fixture in `post-fix/` proves:

- Both Switchboard contexts coexist for 61 seconds with zero configure messages.
- The card's `board_ask` and list's `ask` each follow their own context/brief configuration on the same helper PID.
- The GUI Step limit row sends `max_steps: 37`, and the Tool-call limit row sends `max_tool_calls: 43`, to that helper. No ask or context switch occurs after the limit-edit timestamp. Screenshots 09 and 10 show the changed rows.

The local stub answers all model requests; no external account/model is used. `worker-tap.py` transparently forwards selected input messages to the real worker. The captured run explicitly blurred both number editors by clicking the Options search field; the driver now performs those clicks inline. Tab now navigates pane tabs and is not a reliable way to commit these edits. The driver targets the named Back/search/number widgets. Reproduce with `bash docs/qa_evidence/2026-09-22-verify-CFG1/drive.sh` (defaults to the `post-fix/` output folder).

Build: `scripts/relay-build --wait-seconds 60 --target relay`, successful in 58 seconds. The live run uses a frozen `/tmp/relay-cfgact-fixed` copy, identified by SHA256 and source HEAD in `post-fix/provenance.txt`; unrelated shared-checkout edits are disclosed there. Landing separately builds the exact commit tree, excluding other sessions' uncommitted hunks.

Checks: `consolemode` and `agentcontext` passed in recorded run `20260922T012657Z-5754`, no signals opened. `PYTHONPATH=backend:tests python3 -m unittest test_security.SectionPlacementTests` passed all four cases. The live protocol assertions prove propagation, which those existing unit checks do not cover.

Parent coordinated the shared-header edit with MDL1 and explicitly authorized it. No further owner approval was needed. The fix is delivered for independent review of this post-fix evidence.
