# #7BM4 attribution repair

Date: 2026-09-21 America/New_York (2026-09-22 UTC).

The baseline verifier found that moving an already attributed card from
`needs-verification` into a QA lane replaced `implemented_by` with the verifier's
signature. The repair preserves a non-empty existing implementer through verifier
section updates and QA transitions. An unsigned card still receives the worker's
signature, and an unsigned guest transition may still supply `implemented_by`.

## Verification

- `python3 -m unittest tests.test_board_tools.SignatureTests` — 11 passed.
- `python3 -m unittest tests.test_board_tools` — 257 passed.
- `python3 -m unittest tests.test_test_probe tests.test_test_history tests.test_junit_runner tests.test_tests_protocol tests.test_profile_protocol tests.test_relay_profile` — 237 passed. One existing `ResourceWarning` in `test_profile_protocol.py:192`; no failure.
- `ctest --test-dir build -R '^(testsuites|cardtests|profilepane|windowstate)$' --output-on-failure` — 4 passed.
- `python3 scripts/relay-board.py check` — checked 456 cards; its 12 errors and 749 warnings were in other cards/threads, with no diagnostic for `#7BM4` or its card path.

The regression specifically writes a verifier `## QA checklist`, moves the card into
`needs-qa-llm`, and passes a conflicting `implemented_by`; the original
`anthropic/claude-fable-5.1` value remains unchanged.
