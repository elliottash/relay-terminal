# #8NCF implementation evidence

Failed or unusable title calls no longer advance title_turn or clear compaction staleness.
The next finished turn retries; successful refreshes and user titles retain their cadence.
Reason-only title_failed events identify setup/call failures, unavailable guest side providers,
empty transcripts and unusable replies. No exception message or transcript is logged.
Plain-text title replies remain supported; JSON with a missing/non-string title is rejected.

Validation:
- `scripts/test.sh --junit /tmp/8ncf-tests.xml tests.test_titles tests.test_sessions`: 69 passed (tests.xml).
  The planned pytest command could not run because pytest is not installed; the same unittest modules ran.
- `scripts/relay-build --target relay-titles-tests`: passed.
- `ctest --test-dir build -R titles --output-on-failure`: 1 passed.
- `python3 docs/qa_evidence/2026-09-22-title-retries/drive.py`: passed in isolated Xvfb/XDG directories.
  header.png was visually inspected: the pane header shows “Fixing pane title retries”.
  This uses a protocol fixture with the existing GUI binary; worker failure/recovery is covered
  by scripted-provider tests, not an end-to-end live paid-provider/bad-key run.
- Board-wide check reports pre-existing errors/warnings elsewhere; no unrelated cards were edited.

The GUI already consumes session_title via setTitleFromWorker in src/Pane.h; no GUI edits needed.
