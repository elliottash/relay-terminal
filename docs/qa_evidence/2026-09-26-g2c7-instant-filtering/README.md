# #G2C7 — instant title and #code filtering with highlights

Landed: commit 64c95e0a (2026-09-26), `scripts/land.py` verify-slot build green.

## Commands run (working tree at commit time)

- `PYTHONPATH=backend python3 -m unittest tests.test_conv_index` — **132 tests OK**
  (new: card codes collected and returned; meta_only lists every conversation;
  v8 database gains its codes in place; codes cap).
- `QT_QPA_PLATFORM=offscreen ./build/relay-conversations-tests` — **59 passed, 1 failed**
  (the failure is #TJJ3, pre-existing at HEAD: #E8V1 renamed the helper row to
  "Agent" and left the test expecting "Helper Agent").
  New tests: columnsAreBackUnderTheStraddlingTitle,
  instantTitleFilterOrdersExactFirstAndMarksTheMatch,
  codeChipsFilterInstantlyAndBoldClaimedOnes,
  fullTextReplyPutsTheMatchedLineInTheSubLine.
- `QT_QPA_PLATFORM=offscreen ./build/relay-boardfilter-tests` — **6 passed**
  (new: theFilterRanksItsMatchesAndMarksWhereTheyLanded).
- `PYTHONPATH=backend python3 -m unittest tests.test_session_protocol` — 38 tests,
  1 pre-existing failure (test_compact_resume_recap_and_plan_execute), from other
  sessions' uncommitted backend work; this change's diff there is only the
  `meta_only` flag parse and pass-through.
