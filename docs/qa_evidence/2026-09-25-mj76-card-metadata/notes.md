# #MJ76 implementation evidence

Landed code:

- `510de4f1`: optional schema, validation, computed dates and reverse links, move defaults.
- `850876a2`: `land.py` commit recording and focused Python tests.
- `3512773d`: Board rows and card page for owner, resolution, due dates, snooze, children,
  reverse links and commit sequence; policy and protocol docs.
- `43528a38`: tests_check and QA use the final hash of the ordered sequence as the revision.

Checks on 2026-09-25:

- `git archive 43528a3843de` in a temporary directory, then
  `PYTHONPATH=backend python3 -m unittest tests.test_board tests.test_board_tools tests.test_land tests.test_qa_verifiers tests.test_tests_protocol -q`:
  **703 tests OK, 1 skipped**.
- `scripts/relay-build --target relay-board-tests relay-boardpane-tests`: exit 0.
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(board|boardpane)$' --output-on-failure`:
  **2/2 passed**.
- `scripts/land.py commit` built the exact `3512773d` tree before updating main.
- `board_read #MJ76` reports `links.commits` in order:
  `510de4f150cb`, `850876a27cce`, `3512773d0179`, `43528a3843de`.
- Current `TestsCommands.check_card('MJ76')` reports revision `43528a3843de` and no
  missing-evidence finding. The running Relay Board bridge still has its old imported
  `tests_protocol.py` and reports `510de4f150cb` until that worker restarts.

The separate verifier should open a Board card with due/owner/children/commit metadata and inspect
the rendered row and page, including the Snoozed filter and commit diff links. This file records
the implementer's checks; it is not a QA verdict.
