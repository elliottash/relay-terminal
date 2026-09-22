---
id: PR4Q
type: work
status: needs-qa-llm
labels: [feature, switchboard, tests]
component: [gui, worker]
parent: YZ8G
rank: zzzzzzzzzzzzzzzza
created: '2026-09-21'
source: 'owner, 2026-09-21: "i agree with all, go ahead with it" (#YZ8G plan)'
links: {plans: [], commits: [bebbe33677a3, 60b3fa43ece4, 2d1901e6613c, 630db31e05e3, c3a88caada6f], evidence: [docs/qa_evidence/2026-09-21-check-gate-statuses/, docs/qa_evidence/2026-09-21-verify-PR4Q/], related: [YZ8G, 7BM4], github: null}
---
# Check and the gate: four statuses, attached results, retired tests, scoped overrides, one status not a pile of blocks

## Issue
Step 1 of #YZ8G's plan, from Codex's review (docs/research/qa-across-fields/f-codex-skeptical-review.md §C): "Make the status distinguish passed, failed, missing evidence and not applicable. Block acceptance for explicitly required checks with applicable failures or missing evidence. Offer Run, Use this existing result, or Replace retired check. Keep incidental historical findings advisory … An override becomes a rubber stamp when the same known flake, unsupported runner or local-history gap prompts it repeatedly. Record an exception scoped to the check, relevant environment and revision or expiry … a card without ## Tests is ungated. That rewards omitting evidence." And §B: "Show the latest status; retain history behind a link"; "Update evidence status automatically after runs".

## Done means
- Check answers per listed test with one of: passed, failed, missing evidence, not applicable — never "never run here" when a run from any host or an attached result exists for this revision.
- An attached result (a JUnit file, a `relay-remote-tests` folder, a CI artefact) tied to a revision counts as evidence; the card page offers "Use this existing result".
- A listed test absent from discovery reads "retired" with the action "Replace retired check"; it does not by itself block.
- The gate blocks only on required checks with an applicable failure or missing evidence; an override is scoped to (check, revision) and expires; the same override is never asked twice for the same check and revision.
- A card without `## Tests` is gated on its `acceptance`/`## Done means`: the move to QA asks for the checks that prove it, once.
- The card body carries one current `### Check` status, replaced in place; history is one link to the thread.
- A finished run refreshes the strip and the findings without another Check click.
Failure would show as: an override prompt repeating for a known flake; "never run" on a test sphinxpad ran yesterday; a dated block pile; a card with no Tests section sailing through.

## Tests
- `tests/test_test_history.py::StatusTests` — tests/test_test_history.py
- `tests/test_tests_protocol.py::GateTest` — tests/test_tests_protocol.py
- `tests/test_tests_protocol.py::AcceptResultTest` — tests/test_tests_protocol.py
- `tests/test_tests_protocol.py::CheckBlockTest` — tests/test_tests_protocol.py
- `tests/test_tests_protocol.py::RefreshAfterRunTest` — tests/test_tests_protocol.py
- `ctest -R cardtests` — tests/cardtests_test.cpp
- manual: docs/qa_evidence/2026-09-21-check-gate-statuses/

## QA checklist
Revision checked: `d6682f97` — the newest in `links.commits`' chain (the card's five are
`bebbe33677a3`, `60b3fa43ece4`, `2d1901e6613c`, `630db31e05e3`, `c3a88caada6f`). Everything below
is a claim about that revision and no other, made on a clean export of it
(`git archive d6682f97 | tar -x -C $X`), never in the shared checkout. Fixture: the #7BM4 staged
`orders` project — a passing check, a failing one, a retired one, one that has never run, and ten
days of history from two hosts — plus a card with no `## Tests` at all.

`## Done means`, line by line:

- **Four statuses per listed test, never "never run here" when a run from any host or an attached
  result exists for this revision** — *passed*. One Check on the staged card answers
  `passed ctest:totals` (the laptop's run of this revision), `failed ctest:totals_large_order`,
  `not-applicable ctest:rounding`, `missing-evidence unittest:tests.test_invoice`, and the
  missing-evidence sentence is "no run … for this revision, from any host, and no attached
  result" — not "never run here". Evidence:
  `docs/qa_evidence/2026-09-21-verify-PR4Q/03-four-statuses.png`, `backend-drives.txt` drive1
  (the evidence rows behind each status), `tests.txt`
  (`tests.test_test_history.StatusTests`, 12 tests, OK).
- **An attached result tied to a revision counts as evidence; the card page offers "Use this
  existing result"** — *passed*. Drive5 drops a `relay-remote-tests` folder (`meta.json` +
  `unittest.xml`, this card's commit) into the board's `incoming/`; `ingest_incoming` folds it in
  and `unittest:tests.test_invoice` goes `missing-evidence` → `passed … on sphinxpad`, and the
  gate stops blocking on it. The offer is on the card page and sends `tests_accept`. Evidence:
  `backend-drives.txt` drives 4–5, `03-four-statuses.png` (the button), `04-accepted.png`,
  `tests.txt` (`AcceptResultTest`, 4 tests, OK).
- **A listed test absent from discovery reads "retired" with "Replace retired check", and does not
  by itself block** — *passed*. `ctest:rounding` reads `not-applicable · ctest -R rounding is not
  in the project any more`; the action opens the `## Tests` lines in a box whose hint names that
  check; and the gate's refusal lists only `totals_large_order` and `test_invoice`. Evidence:
  `05-replace-retired.png`, `07-gate-refusal.png`, `backend-drives.txt` drives 2–3, `tests.txt`
  (`GateTest::test_a_retired_test_does_not_block_the_landing`).
- **The gate blocks only on failed/missing evidence; the override is scoped to (check, revision),
  expires, and is never asked twice for the same check and revision** — *passed*. Needs
  verification → Done is refused with one sentence; Override… asks once; the move lands; the
  thread's `decision` entry carries one marker per waived check
  (`<!-- relay:override test=… rev=307ac063fe7b until=2026-10-05 -->`); moving the card back and
  to Done again lands it with no question at all. `live_overrides` returns those checks for that
  revision and nothing for another one. Evidence: `07-gate-refusal.png`,
  `08-override-asked.png`, `10-override-recorded.png`, `13-not-asked-again.png`,
  `backend-drives.txt` drive3 A–D, `tests.txt` (`GateTest`, 13 tests, OK).
- **A card without `## Tests` is asked, once, which checks prove it, then moves** — *passed*. The
  no-`## Tests` card's first move towards a QA lane is answered with "Which checks prove #5XQ5?
  One per line." with "None apply" beside Save; `<!-- relay:tests-none card=5XQ5 -->` is recorded
  as a note; the second and third attempts go through in silence. Evidence:
  `17-which-checks-prove-it.png`, `18-none-apply-recorded.png`, `backend-drives.txt` drive2,
  `tests.txt` (`GateTest::test_a_card_with_no_tests_section_is_asked_once_and_then_moves`).
- **One current `### Check` status, replaced in place, history one link to the thread** —
  *passed*. Three consecutive checks leave exactly one `### Check` heading in the card, ending
  `history: thread`, with the four `## Tests` lines byte-for-byte untouched above it; each check
  appends its own `evidence` entry to the thread. Evidence: `backend-drives.txt` drive4 F,
  `tests.txt` (`CheckBlockTest`, 7 tests, OK).
- **A finished run refreshes the strip and the findings without another Check click** — *passed*.
  `21-before-run.png` is a fresh Check ("1 passed · 1 failed · 1 missing evidence · 1 not
  applicable"); "Run these" was clicked once and `22-run-started.png` is the same strip after the
  run with no second Check — "2 passed · 1 failed · 1 not applicable", `tests/test_invoice.py`
  now `passed … on spark-dcc9`. Evidence: those two captures, `backend-drives.txt` drive4 H,
  `tests.txt` (`RefreshAfterRunTest`, 2 tests, OK).

`## Tests`, line by line — all run on the clean export, transcripts in
`docs/qa_evidence/2026-09-21-verify-PR4Q/tests.txt`:

- `tests/test_test_history.py::StatusTests` — *passed*: `python3 -m unittest -v
  tests.test_test_history.StatusTests` → `Ran 12 tests … OK`.
- `tests/test_tests_protocol.py::GateTest` — *passed*: `Ran 13 tests … OK`.
- `tests/test_tests_protocol.py::AcceptResultTest` — *passed*: `Ran 4 tests … OK`.
- `tests/test_tests_protocol.py::CheckBlockTest` — *passed*: `Ran 7 tests … OK`.
- `tests/test_tests_protocol.py::RefreshAfterRunTest` — *passed*: `Ran 2 tests … OK`.
  (Both modules together: `Ran 170 tests … OK`.)
- `ctest -R cardtests` — *passed*: built from the export (`cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`,
  targets `relay` and `relay-cardtests-tests`), `ctest --test-dir $X/build -R '^cardtests$' -V` →
  `Totals: 15 passed, 0 failed`, including `theFourStatusesAreDrawnAboveTheAdvisoryFindings`,
  `useThisExistingResultAcceptsTheRunItNames`, `replaceRetiredCheckOpensTheTestsSection`,
  `aCardWithNoTestsIsAskedWhichChecksProveIt`, `noneApplyRecordsTheAnswerAndSendsTheMoveAgain`.
- `manual: docs/qa_evidence/2026-09-21-check-gate-statuses/` — *passed*: every capture in the
  implementer's folder was read and then reproduced here on a different staging, with different
  card ids and a different commit; `card-ZH1E.md`, `thread-ZH1E.md` and `thread-800P.md` match
  the artefacts this session's own drive produced. Nothing in it overstates what the code does.

tests: passed (revision d6682f97)
simulation: played (evidence docs/qa_evidence/2026-09-21-verify-PR4Q/)
staged: docs/qa_evidence/2026-09-21-verify-PR4Q/

Unresolved, not a finding against the bullets above: **the agent's own `board_move_card` is not
gated.** The gate lives in `board_protocol._tests_gate`, which the Switchboard's `board_move`
request goes through; `BoardTools.run("board_move_card", …)` — what a pane agent or a guest CLI
calls — does not. Drive6 lands the staged card straight to `done` that way, with no question, no
override and no marker on the thread, while the same move from the GUI is refused. It is outside
this card as written: `## Done means` names no move path, and the gate has only ever been in
`board_protocol` (`git show bebbe33677a3^:backend/relay_core/board_protocol.py` already has
`_tests_gate` — it came with #7BM4, before this work). Whether an agent should be able to land its
own card without answering the gate is a product decision, so it is left for the owner rather than
answered here. Evidence: `docs/qa_evidence/2026-09-21-verify-PR4Q/backend-drives.txt`, drive6.

Reviewed by Claude Opus 5 (claude-opus-5[1m], verifier session) on 2026-09-21: no findings.

## Verdict
Passed. All seven `## Done means` lines and all seven `## Tests` lines check out at revision
`d6682f97`, verified on a clean export of it and never in the shared checkout: the two unittest
modules (170 tests, OK) and each named class on its own, `ctest -R cardtests` (15 cases, all
passed) built from that export, six backend drives through `TestsCommands` and the real
`board_protocol` move path, and a live drive of the #7BM4 staged `orders` project under an
isolated Xvfb profile with xdotool only — the four statuses on one Check, the laptop's result
offered and accepted, the retired check named and replaceable, the refused move with its one
question, the override asked once and not again, "Which checks prove this card?" answered with
None apply, and the strip refreshing itself after a run. Nothing failed and nothing was left
without evidence. One thing is unresolved and left for the owner: the agent-side
`board_move_card` bypasses the gate, which pre-dates this card (#7BM4) and which `## Done means`
does not speak to.
