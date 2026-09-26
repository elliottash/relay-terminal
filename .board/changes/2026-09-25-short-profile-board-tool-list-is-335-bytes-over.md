---
id: BYHN
type: work
status: planned
labels: [bug, prompt]
discovered_from: K54A
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [K54A, MJ76], github: null}
---
# Short-profile board tool list is 335 bytes over its budget at HEAD

## Issue
tests/test_prompt_profiles.py::AgentTests::test_a_board_adds_five_tools_and_the_policy_and_nothing_else fails at HEAD: the short (Local/Lite) tool list with a board is 11,599 bytes against the 11 KiB (11,264) budget. Found while fixing #K54A. The same failure appears on a clean git archive of HEAD with PYTHONPATH set to the export. Growth is in the five board schemas (board_create_card 2,683 B, board_list 1,584, board_claim 1,162, board_comment 963, board_read 600); board_create_card's parent/related/metadata fields came with #MJ76 (510de4f1). The test says these five are sent byte for byte as in the full profile, so trimming means editing board_tools.py TOOL_SPECS, which other land sessions currently claim. Fix needs a trim there, or an owner decision to raise the budget.

## Done means
The short (Local/Lite) tool list with a board is back under its byte budget at HEAD: `tests/test_prompt_profiles.py::AgentTests::test_a_board_adds_five_tools_and_the_policy_and_nothing_else` passes, and the byte-for-byte companion `test_the_board_five_keep_the_rules_decision_8_moved_into_them` stays green — the five board tools carry every Board rule (decision 8, #GMCF) in both profiles, so no rule was deleted to fit. Failure is recognised as the budget test failing again after the next feature lands on the five board specs, or as any diff between the short and full versions of those five.

## Plan
### Goal
Get the short (Local/Lite) tool list with a board back under its byte budget at HEAD — 11,599 B today against the 11,264 B (11 KiB) budget — without deleting a Board rule from the five board tools (decision 8, #GMCF) and without touching files beyond `backend/relay_core/board_tools.py` and `tests/test_prompt_profiles.py`.

### Findings
- `backend/relay_core/prompt_profiles.py:107` fixes `SHORT_BOARD_TOOLS` = board_list, board_read, board_create_card, board_claim, board_comment; `tool_specs()` (line 217) sends those five **byte for byte as the full profile** — enforced by `tests/test_prompt_profiles.py:197`, which also forbids adding them to `SHORT_DESCRIPTIONS`. So the only trim is in `board_tools.py` `TOOL_SPECS` (line 195), shared by both profiles.
- The five specs are 7,111 B of the 11,599 B list. Since the 10.2 KB baseline of #GMCF (`cd356730`, 2026-09-20) they grew **+1,152 B**: board_create_card +637 (#MJ76 parent/related `510de4f1`, #EMWF quote rule) and board_list +529 (#EE42 link filters/case args `97da1c60`, #ZB9M ceremony metering `668e381a`). Measured by exec-ing `TOOL_SPECS` from each revision.
- Prose fat that can come out **without losing a rule**, measured with drafted rewrites: board_create_card description −55 B, its `labels` −48 B, `summary` −12 B, board_claim description −54 B, board_list `cases` −21 B, `_ID_ARG` ("Card id, four characters…"; used by board_read/board_claim/board_comment) −63 B over its three uses. Total ≈ **250 B — not the 335 needed**. The rest of the spec text is teaching information (status/tab enums, field lists) or rules; cutting it deletes information, not fat.
- Pinned text: `board_claimed_elsewhere` must stay in board_claim's description (`tests/test_board_tools.py:3268`).
- `board_tools.py` is claimed by three live land sessions right now (`land.py who`: pane ca66ac8d, fyey-py, relay-mj76).

### Steps
1. Take the owner's answer to the budget question (below). Recommended: **raise the budget 11 → 12 KiB** in `tests/test_prompt_profiles.py:195` — the +1,152 B since the baseline is real feature growth on the five tools, and the trim alone cannot reach 335 B without cutting rules or teaching text.
2. `python3 scripts/land.py begin <me> backend/relay_core/board_tools.py tests/test_prompt_profiles.py`, then trim the measured fat in `TOOL_SPECS` (~250 B, Findings): tighten board_create_card's description, `labels` and `summary`; board_claim's description; board_list's `cases`; `_ID_ARG`. Keep every rule: the #EMWF quote rule, the duplicate-check `not_duplicate_of` override, labels rule 12 ("exactly one of 'bug' or 'feature'"), `board_claimed_elsewhere`, ask-before-force.
3. Update the test's measurement comment (currently "Measured 2026-09-20: 6.6 KB prompt and 10.2 KB tools") to the new numbers, so the budget's provenance stays honest.
4. If the owner holds 11 KiB instead: the trim then needs a further ~100–350 B, and the only cut that big is board_list's case-ledger surface (`cases`/`server`/`card` args, ≈ 500 B, #95VZ/#ZB9M) — dropped from `board_list` for **all** profiles. That is a feature removal, so it happens only on the owner's explicit say-so, not the Run agent's judgement.
5. `python3 scripts/land.py try <me> --tests 'prompt_profiles|test_board_tools'`, then `land.py commit <me>`. Expect the contested-hunk confirm review (three sessions hold the file) — keep the trims inside the five specs so every hunk is small and legible.

### Risks
- **Owner decision (asked as a question on this card):** raise the budget to 12 KiB, or hold 11 KiB and accept a feature cut (step 4)? A raised budget buys ~1.4 KiB headroom; holding it means this test breaks again at the next #MJ76-scale change.
- Three live sessions hold snapshots of `board_tools.py`; their uncommitted edits may interleave with the trim hunks. The confirm digest and `--exclude-hunk` are the escape; never hand-merge over their text.
- Trimming descriptions changes the full (BYOK) profile equally — the byte-for-byte test covers it, but any test pinning spec prose beyond `board_claimed_elsewhere` would catch us; grep before committing.

### Verify
- `PYTHONPATH=backend python3 -m pytest tests/test_prompt_profiles.py -q` — the budget test (line 168), the byte-for-byte test (197), the schemas test (275) and the reminder test (289) all green.
- `PYTHONPATH=backend python3 -m pytest tests/test_board_tools.py -q` — one file, pins the description strings and behaviour.
- Print the new short-list total (`len(json.dumps(tools))` as the test computes it) and record it in the test comment.
