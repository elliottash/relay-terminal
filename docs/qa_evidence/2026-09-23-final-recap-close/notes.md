# Final recap on session close (#RCP9)

2026-09-23. The close path now starts `relay_core.final_summary` in a separate process when
the last assistant turn is newer than the saved recap. The process reads the saved session and
writes only metadata and the index. The existing recap remains if the provider is unavailable,
returns unusable text, or fails. A result for a session that advanced while the model ran is
discarded.

Checks run:

- `PYTHONPATH=backend python3 -m unittest tests.test_final_summary tests.test_summaries tests.test_titles tests.test_sessions tests.test_conv_index tests.test_session_protocol` — 258 tests passed.
- `scripts/relay-build --target relay` — built successfully.
- `python3 -m compileall -q backend/relay_core/final_summary.py backend/worker.py` — passed.

`tests.test_final_summary.test_close_starts_a_separate_process_that_finishes_after_return`
starts a helper from a short-lived Python parent. The fake model server withholds its answer
until that parent has exited; the final recap still appears in the session metadata. Other
focused cases cover failed refresh provenance, no chores provider, failed model calls,
resumption while the call runs, metadata preservation, index rebuild and adoption on resume.
