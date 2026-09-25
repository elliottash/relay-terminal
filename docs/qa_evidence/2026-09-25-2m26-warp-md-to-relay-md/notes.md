# #2M26 evidence — 2026-09-25

## WARP.md is gone; RELAY.md is the file
ls: cannot access 'WARP.md': No such file or directory
-rw-rw-r-- 1 elliott elliott 5064 Sep 24 16:29 RELAY.md

## Only the allowlist still mentions WARP.md
$ rg -l "WARP\.md" --hidden -g "!.board" -g "!build*" -g "!research_notes" -g "!reports" -g "!tests/fixtures" . | grep -v "^\./\.git"
./backend/relay_core/instructions.py
./tests/test_board.py
./tests/test_session_protocol.py
./docs/qa_evidence/2026-09-25-2m26-warp-md-to-relay-md/notes.md
(instructions.py and the two test files keep it deliberately: legacy-name support and its tests)

## Discovery prefers RELAY.md
RELAY.md ahead of WARP.md in PROJECT_ORDER: True

## Tests
$ PYTHONPATH=backend python3 -m unittest tests.test_session_protocol tests.test_board
Ran 194 tests in 31.403s

OK
