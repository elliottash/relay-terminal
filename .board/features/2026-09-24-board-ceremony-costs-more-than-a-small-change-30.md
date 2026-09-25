---
id: ZB9M
type: work
status: needs-verification
labels: [feature, board, tokens, deliver]
assignee: agent
implemented_by: kimi/k3
session: ca66ac8d-f133-4333-adbd-b69a70ec1ba6
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {commits: [668e381ad13d], evidence: [], github: null, plans: [], related: [234Z, P7CF]}
---
# Board ceremony costs more than a small change: ~30 requests and ~4M tokens of card, evidence and Try-it work for a 50-line fix

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** Session `3f4a20ad` (#234Z): the change is ~50 lines of behaviour in `Pane.h` (173 with tests and docs). The `deliver` skill tiered it **large** ("keyboard behavior + visible UI text"). Around it came: two `board_create_card` calls (the first refused), `board_claim`, two `board_update_card` for the plan and verify block, `land.py begin` and `who`, and a second land session and commit for `docs/qa_evidence/2026-09-25-234Z/README.md`. Close-out took four more board writes and a `board_read`. Then `board_try` staging, including a full `relay` build, which found a stale binary (separate card) and was interrupted by the user. In total about 30 requests and ~4M prompt tokens, 20% of the turn. Every step re-sends ~100k tokens of context, so ceremony costs steps, not words.

**How to address** (needs an owner decision on 1 and 3):
1. Tiering: visible text changing inside an existing control should not by itself make a change large. "Needs eyes" should mean new or rearranged UI. Keyboard behaviour with a script test is medium.
2. Evidence goes in the one commit, never a second commit: the test output in the commit message or the card's `## Tests`, and `docs/qa_evidence/` only for artifacts that are not text.
3. Try-it staging only when the card's `verify` block names `person` as primary, or when the user asks.
4. One close-out write: `board_move_card` accepts the execution summary and tests sections, so landing is one call and not five.
5. Measure: count board and land calls per card in the case ledger, and flag cards where ceremony steps exceed implementation steps.

**Done means.** On a replay of #234Z the procedure takes ≤10 non-implementation steps; the deliver skill's tier table reflects the decision.

## Done means
A replay of a #234Z-sized change takes ≤10 non-implementation steps: the tier table counts visible-text-only changes as medium, evidence rides the one commit (text in the commit message or `## Tests`; `docs/qa_evidence/` only for non-text artifacts), close-out is a single `board_move_card` call carrying `## Execution Summary` and `## Tests`, and `board_try` refuses unless the card's verify block names `person` (the owner's Try it button is unaffected). The case ledger gains a ceremony row per landed card, and `board_list {cases: true}` flags rows where ceremony steps exceed implementation steps. Failure shape: an agent still files a second evidence commit, or a replay still costs ~30 steps.

## Plan
**Goal.** Cut the fixed ceremony around delivering a small change through the board (measured on #234Z: ~30 requests, ~4M prompt tokens for a ~50-line fix) to ≤10 non-implementation steps, by fixing the deliver procedure's text where it over-tiers and over-documents, and adding three small code changes: a one-write close-out, a `board_try` gate, and a ceremony row in the case ledger.

**Findings.**
- The procedure text has two sources: `backend/relay_core/board_policy.md` (rule 1, "Move it") and `backend/relay_core/skills_bundled/deliver/SKILL.md` (`## 0. Which tier?`, landing step 5). `.board/POLICY.md` is generated from both — regenerate with `python3 scripts/relay-board.py policy`, never hand-edit.
- `board_try`: spec at `backend/relay_core/board_tools.py:415`, `_board_try` at 3157. The Board's Try it **button** is a person acting, so the gate belongs in the agent tool only.
- `board_move_card`: `_move` at `board_tools.py:2582` is the one release function both the tool and the board `board_move` message use (see `tests/test_board_tools.py:1468`), so one change covers both.
- Case ledger: `backend/relay_core/cases.py` (`CASES_FILE = "cases.jsonl"`), rows carry `server`, `served_by`, `cost`, `signal`; `board_list {cases: true}` lists them.
- Tests live in `tests/test_board_tools.py` and `tests/test_board.py`.

**Steps.**
1. Tier wording (Q1): in the deliver skill's tier table and `board_policy.md` rule 1, replace "changes UI (needs eyes)" with "adds or rearranges UI (needs eyes)", plus one sentence: visible text inside an existing control is not a UI change; keyboard or other behaviour with a script test is medium.
2. Evidence in one commit: in the skill's landing step and the policy's "Move it" section — text evidence (test output) goes in the commit message or the card's `## Tests`, in the same commit; `docs/qa_evidence/` only for artifacts that are not text; never a second commit.
3. Try-it gate (Q3): `_board_try` refuses with code `board_refused` and a one-line reason unless the card's `verify.primary` is `person` (or the verify block requires human); update the spec text at 415.
4. One-write close-out: `_move` accepts `sections: {heading: text}` (e.g. `## Execution Summary`, `## Tests`) written through the same card-write path as the move, so landing is one call; update the spec text and the skill's landing step to say so.
5. Measure: at landing into done/needs-verification/needs-qa-*, `_move` appends a case-ledger row `server: "deliver"` with `signal: {ceremony_steps, implementation_steps}` — ceremony = the card's board-write thread events (+ land.py calls if a session record is reachable), implementation = commits in `links.commits` touching code; `board_list {cases: true}` flags rows where ceremony > implementation.
6. Regenerate `.board/POLICY.md`, run `python3 scripts/relay-board.py --board .board check`, commit everything in one commit with the test output.

**Verify.** Extend `tests/test_board_tools.py`: `_move` with `sections` lands the sections in one write; `board_try` refuses without a person-primary verify block and passes with one; landing writes the case row and `board_list` flags it. Run the touched test files (targeted, not the suites).

**Risks / owner decisions.**
- Q1 — tier rule: confirm the wording in step 1 ("needs eyes" = new or rearranged UI; visible-text-only inside an existing control is medium). `/deliver`-forced largeness is unchanged.
- Q3 — try-it gate: confirm the gate keys on the verify block only. The tool cannot reliably see a conversational "stage it anyway"; if you want that to unlock staging, say so and step 3 grows an `allow_once` answer the agent can carry.
- Step 5's "implementation steps" proxy (code commits) is crude; refine if you count otherwise.

## Execution Summary
All six plan steps landed in one commit, `668e381a` (`#ZB9M` in its message, trailer `Implemented-By: anthropic/claude-opus-5-5`):

1. **Tier wording** — `deliver` SKILL.md tier table now reads "adds or rearranges UI (needs eyes)" with a sentence: visible text inside an existing control is not a UI change; script-testable behaviour is medium. `board_policy.md` rule 1 says the same in one line (policy block kept under 3 KB).
2. **Evidence in one commit** — the skill's landing step and the policy's Move-it text now say text evidence (test output) rides in the commit message or `## Tests`; `docs/qa_evidence/` is only for non-text artifacts; never a second commit. This card lands that way: its evidence is the commit plus `## Tests`, and `links.evidence` is empty.
3. **Try-it gate** — `_board_try` refuses with `board_refused` unless `verify.primary` is `person` or `verify.human` is `required`; spec text updated. The Board's Try it button is untouched (a person acting).
4. **One-call close-out** — `board_move_card` accepts `sections: {heading: text}` written in the same card write as the move (replace semantics; `## Issue`, `## QA checklist`, `## Verdict` refused); spec and skill landing step updated. Covered by `test_sections_land_with_the_move_in_one_write`.
5. **Ceremony metering** — landing a work card into done/needs-verification/needs-qa-* appends a `server: deliver` case row with `signal {ceremony_steps, implementation_steps}` (thread board-write events + 2 per reachable land session, vs commits in `links.commits` touching paths outside `.board/` and `docs/qa_evidence/`); `board_list {cases: true}` flags those rows `ceremony>implementation` when ceremony outnumbers implementation. `cases._signal` learned the two integer keys.
6. **Regeneration** — `.board/POLICY.md` regenerated with `scripts/relay-board.py policy`; `scripts/relay-board.py --board .board check` reports 1 pre-existing error on another card (`#P7CF`'s verify block missing `effort`) and 1334 pre-existing warnings, none from this change.

Owner decisions Q1/Q3 were taken as the plan proposed (the owner moved the card to Planned and ran it unchanged): "needs eyes" = new or rearranged UI, and the try-it gate keys on the verify block only — no conversational unlock. A `#ZB9M`-sized replay now costs: claim, land begin, one commit, one `board_move_card` with `sections` — under the ≤10 target.

Dogfood note: this close-out itself used the new path one step late (the move ran without `sections`), so the summary and tests were two `board_update_card` calls instead of zero — the mechanism works (tested), the habit is next.

## Tests
Targeted, not the suites (policy rule: run what covers the change).

```
$ PYTHONPATH=backend python3 -m pytest tests/test_board_tools.py -q
335 passed in 8.57s

$ PYTHONPATH=backend python3 -m pytest tests/test_board.py -q
157 passed in 2.27s

$ PYTHONPATH=backend python3 -m pytest tests/test_cases.py -q
15 passed in 0.49s

$ python3 -m py_compile backend/relay_core/board_tools.py backend/relay_core/cases.py backend/relay_core/board.py
(no output)

$ python3 scripts/relay-board.py policy
wrote .board/POLICY.md

$ python3 scripts/relay-board.py --board .board check
846 cards, 6 types, 12 sections … 1 error (pre-existing, card #P7CF), 1334 warnings (pre-existing)
```

New tests in `tests/test_board_tools.py`:

- `test_sections_land_with_the_move_in_one_write` (MoveTests) — a `board_move_card` with `sections` writes `## Execution Summary` and `## Tests` in the same card write, one thread entry naming both.
- `test_sections_refuse_the_sections_that_are_not_the_movers_to_write` (MoveTests) — `sections: {Verdict: …}` is refused `board_refused`, card untouched.
- `TryItGateTests` (3 tests) — `board_try` refuses a `verify.primary: script` card; stages for `person`; stages for `human: required` (tryit_prompt mocked; no build).
- `test_landing_meters_ceremony_against_implementation` (CaseLedgerTests) — landing writes the `deliver` row (`ceremony_steps > 0`, `implementation_steps 0`, verdict pending); `board_list {cases: true}` shows `flag: ceremony>implementation` for it and no flag for a row whose implementation outnumbers ceremony.
- `test_a_move_to_done_is_a_pass_and_back_a_stage_is_a_fail` (CaseLedgerTests) — updated: verdict rows are now told apart from ceremony rows by their signal shape (`ceremony_steps` present), and the verdict sequence fail/pass/pass is unchanged.
