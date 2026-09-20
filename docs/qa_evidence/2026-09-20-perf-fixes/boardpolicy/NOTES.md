# Tiering the Switchboard policy (#GMCF, prompt-distillation decision 8)

`docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/PROPOSAL.md` section 2.4 and section 6
item 8: keep in `backend/relay_core/board_policy.md` only what has to be read *before* a board tool
is called, and read the rest where the same request already states it — the tool's own description
(present exactly when the tool is) or the bundled `deliver` skill (loaded when a medium or large
request starts). **Nothing was deleted.** `issues/POLICY.md`, which a guest with no `board_*` tools
reads, is generated from the policy **plus** that skill plus the file-edit appendix, so every moved
sentence still reaches a guest.

## Before / after

`python3 docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py` ("board attached, build
mode"), tokens at the script's 4-bytes-per-token estimate:

| | before | after | delta |
|---|---:|---:|---:|
| board rules (header + `board_policy.md`) | 5,066 B / 1,261 tok | 2,963 B / 738 tok | **−2,103 B / −523 tok** |
| the eight `board_*` schemas | 11,091 B / 2,772 tok | 11,165 B / 2,791 tok | +74 B / +19 tok |
| `tests_check` + `tests_run` | 1,967 B / 491 tok | 2,003 B / 500 tok | +36 B / +9 tok |
| **per board turn** | 18,124 B / 4,524 tok | 16,131 B / 4,029 tok | **−1,993 B / −495 tok** |

`board_policy.md` itself: 5,853 → 4,512 B on disk (most of what stays is the provenance comment,
which is stripped before the model sees it); the rules the model reads, 4,986 → 2,883 B.
`issues/POLICY.md`, the guest's copy: 17,416 → 16,483 B, and it still says everything it said (see
the table below; the diff of the two is this folder's `POLICY.diff`).

The schemas went **up**, not down: the rules that left the policy landed in them. The proposal's
"and the same again if the board tool descriptions are shortened to match" is not reachable by
rewriting description text — the descriptions were already dense, and four of them had to grow to
carry a moved rule. The remaining board-tools saving the proposal costed is the *deferral* of
`board_import_items` (777 B, intake only) and of the app/own-session/tests group (decision 9),
which is tool-list assembly, not description text.

## Every sentence that moved, and where it went

| Left the policy | Now stated by (pane agent) | And for a guest reading `POLICY.md` |
|---|---|---|
| r1 "read the candidate before you create a second card" | `board_create_card` description | `deliver` §2, and the appendix ("a title match is not a match") |
| r2 "`source` says where it came from" | `board_create_card`'s `source` | the appendix's card template |
| r3 "numbered, each with your recommendation" | `board_comment` description | the appendix's `kind` bullet |
| r5 "Relay moves it to `executing` with `assignee: agent`" | `board_claim` description | the appendix's "Claim it" |
| r5 landing: medium → `done` with `links.commits` and the test; large → `needs-verification` with the evidence path and a `## QA checklist`; the verifier moves it on to a QA lane or back | the `deliver` skill §5, and `board_move_card` in one clause | `deliver` §5, which POLICY.md carries in full |
| r5 "Relay stamps `implemented_by` / `verified_by`, never type either" | `board_move_card` description | `deliver` §5, and the appendix ("Never touch `implemented_by`, `verified_by` or `session`") |
| r5 "closing a QA card needs the verdict; the `qa` recommendation names the best verifier" | `board_move_card` description | `deliver` §5 |
| r6 the `## Tests` section, one invocation per line, `tests_check` before needs-verification, fix what it names | `tests_check` description | `deliver` §5 |
| r9 you may rewrite the user's own text; it is recorded; say you did it | `board_update_card` description | `deliver` §2 |
| r12 labels: one of `bug`/`feature` plus area labels, and say nothing about it | `board_create_card`'s `labels` | `deliver` §2, and the appendix's `labels` bullet |

Folded rather than moved: r10 "nothing is deleted / no delete tool / append-only thread / only the
owner may delete" is one clause of the new rule 7; r13 "report `#ID`" is the new rule 9; r11's
ceilings are the `board_rate_limited` refusal's own text and the rule keeps "stop writing and
summarize".

Kept although the draft `BOARD-POLICY.core.txt` dropped them, because each traces to an incident
(the brief's rule: a draft that deletes a rule whose incident is in `git log` is wrong about that
rule): "wherever you ask, the card is where the question waits" including the board page's chat
(#WT9V), decisions also into the card's `## Decisions` section, "never a card for your own working
steps (that is `update_todos`)", and the owner-only delete (#CYM9). That is why the block is 2.9 KB
and not the draft's 2.1 KB.

## Fixed on the way

`board_list`'s `status` parameter offered "inbox, ready, in-progress, needs-qa-llm, done" as
examples. `ready`, `in-progress` and `needs-qa-llm` are legal statuses but no column of a board
written since #3XZV collects them, so the example pointed the model at empty lanes. It now names
the stage statuses the columns do collect. (Flagged in PROPOSAL.md 2.4 as "a bug card for whoever
owns `board_tools.py`" — it is one line of description text, so it is fixed here rather than
filed.) Two comments citing "board_policy.md rule 9" for the no-delete rule (already stale at v4,
which made it rule 10) now cite it by name instead of by number.

## Tests

- `tests/test_system_prompt.py::SizeTests::test_the_prompt_and_the_tools_stay_within_their_budgets`
  — the with-a-Switchboard budget is 12.5 KiB, down from 14 KiB (it measures 11.5 KiB).
- `tests/test_system_prompt.py::SizeTests::test_the_board_policy_block_stays_tiered` — new: the
  block is under 3 KiB, it still carries the six rules that cannot live in a tool description, and
  each moved phrase is *absent* from the block and *present* in the tool spec or the skill it went
  to. It fails on the v4 policy (5,066 B) and on any of the four rules coming back.
- `python3 -m unittest test_board_tools` (221 ok), `test_board` (121 ok),
  `python3 scripts/relay-board.py check` (354 cards, 0 errors).

Pre-existing on a clean export of `main`, not caused by this change:
`test_board_protocol.ProbeAndImportTests.test_apply_creates_the_ticked_cards_and_says_where_they_landed`
and `test_board_protocol.WriteTests.test_a_card_detail_read_round_trips_through_the_protocol`.
`test_system_prompt.StabilityTests.test_attaching_a_switchboard_only_appends` is another session's
uncommitted test (it is not in `main`) and expects the board tools to be appended last, which is a
tool-list assembly change, not this one.

## Not this card's

Whether the Local tier gets a five-tool board (`board_list`, `board_read`, `board_create_card`,
`board_comment`, `board_claim` with this core policy, ≈ 1,500 tokens) or no board at all is the
sub-question of decision 8, and it is the short profile's — pf-assembly's — to answer.
