# #25XG implementer evidence — 2026-09-22

Guard and budget refusals carry strict boolean `refused: true` through results, stored output,
summary labels and the C++ row models. Their unsuccessful verdict, ✗, message, no-merge rule and
click-to-expand behaviour remain intact. OS and decoding failures, nonzero command exits and
string-coded runtime refusals retain error ink.

All four transcript surfaces use neutral ink for refusals. The terminal's hidden Activity rows
retain the grade when replayed; expanded fold errors and the turn view's child/detail messages
also use neutral ink. No new theme token was added.

## Verification

- `scripts/test.sh --junit docs/qa_evidence/2026-09-22-refused-neutral-ink/python.xml tests.test_tool_labels tests.test_agent`: **103 passed**. Covers prepare/execute exceptions, UnicodeDecodeError ordering, model messages, stored output/summaries, budget limits and strict boolean classification. The system Python has no pytest; these unittest modules run through the repository runner.
- `scripts/relay-build --target relay relay-toollabel-tests relay-calllines-tests relay-turntranscript-tests relay-subagents-tests`: passed. A second incremental build covered the expanded-message fixes.
- `ctest --test-dir build -R '^(toollabel|calllines|turntranscript|subagents)$' --output-on-failure`: **4 suites passed**; `ctest.txt` and `ctest.xml` hold the final run. Tests cover parsing, ✗/fold retention, row colours and expanded refusal messages.
- `python3 docs/qa_evidence/2026-09-22-refused-neutral-ink/drive.py`: passed. Uses an isolated Xvfb display and XDG directories, a loopback scripted provider, and real Relay tools. `live.png` visibly shows the muted size refusal above the red failed command. `requests.json` records the real results: the edit on a 140,000-byte file was refused and the file stayed unchanged; `ls /nonexistent` exited 2. No provider account is used.

The screenshot covers the live terminal. Replay grading is covered by result/summary and row
propagation checks; the separate verifier should exercise the live Activity close/replay path.
Board validation reports pre-existing duplicated plan subheadings on #25XG; this work leaves the
approved plan text intact. No implementer QA checklist has been written.
