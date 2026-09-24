---
id: 5KMQ
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: fc9ad858-3122-4955-9ffa-23e0ea9b1dd6
priority: 1
rank: zzzzzzzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [27f849b, 5ac8a99, 36ca8f6], evidence: [docs/qa_evidence/2026-09-23-5kmq/01-draft.png], related: [], github: null}
---
# lite triage of new issues

## Issue
when you enter a new issue, gemini flash lite assigns it to features / bugs / etc.

Priority one is duplicates and related: as you type the first issue text — and right when you press Enter — the composer shows likely-duplicate cards and a related-cards prefill. Tab assignment and the other lite chores can ride the same pass afterwards.

## Decisions
- **2026-09-20 — scope:** duplicates and related are the most important lite chore, and they surface at entry time: live while typing the first issue text and right on Enter, before the card is saved. Tab/labels/title etc. are secondary and can follow in the same pass.

## Planning notes
**2026-09-23 refresh — what the codebase already gives this card:**

- **The `chores` role exists** (`roles.py`): Lite-tier, labelled "duplicate checks, labels, titles, note scans". The model-role half of this card is built; resolve it via `agent.side_provider(role="chores")` exactly as titles do.
- **`route_assist` is the proven template** for the wire: one no-tools side call with a fast dedicated model, debounce in the GUI, timeout/watchdog, and `null` on failure so entry never blocks. The triage pass should copy that shape message-for-message.
- **The local fuzzy check needs no model**: `board_tools.duplicates()` (difflib word/char similarity over open card titles/requests) is cheap enough to run per keystroke with zero API cost.

**Ideas that sharpen the original scope:**

1. **Two-layer duplicates** — local `similarity()` live while typing (instant, free), Lite semantic pass on Enter (catches paraphrases difflib misses). This inverts the original "debounced Lite while typing" plan and is cheaper and faster.
2. **`related:` prefill writes real links** — accepted suggestions land in `links.related` at create time, not as prose in the body.
3. **Same pass serves the chat intake** — cards filed from a Discuss/console conversation get the same duplicate check before `board_create_card`, reusing the existing `possible_duplicates` / `not_duplicate_of` flow.
4. **Guardrail stands**: everything is a suggestion the owner accepts or overrides; never silently applied.

Still nothing built on the GUI side: BoardPane's "+ New card" / quick-add has no entry-time hook — that is the work.

## Done means
While entering a new work card, likely duplicate and related cards appear before save, update with the entry text, and remain reviewable when Enter is pressed.
The owner can open a suggestion, choose or dismiss related links, and save or cancel; accepted links are stored as real card relationships. Suggested tab and labels remain editable.
An unavailable or slow Lite provider does not block card entry, and stale suggestions cannot attach to a different draft. Failure is visible if suggestions arrive only after creation, links are silently applied, or entry stalls.

## Plan
**Goal:** Make new-card triage visible before the first write, with owner-controlled duplicate and related choices.

**Findings:** `src/BoardPane.cpp` currently writes `board_create` on the quick-add field's Enter; `backend/relay_core/board_protocol.py` forwards it to `board_create_card`. The row model has open card IDs and titles but no request text. `board_tools.duplicates()` already supplies the local fuzzy policy, and the `chores` role supplies a Lite side model.

**Steps:** 1. Add a draft/review step to quick add so Enter shows suggestions before save, including immediate local title matches. 2. Add a worker triage request using local fuzzy candidates and a bounded chores-role semantic pass; allow the GUI to ignore stale results and fall back when unavailable. 3. Carry only owner-accepted related IDs and selected tab/labels through `board_create` to real card fields. 4. Add focused protocol/UI tests and document the wire.

**Risks:** Quick add's current Enter behavior changes; keep the draft keyboard path clear, allow cancellation, and do not turn suggestions into automatic writes. Shared checkout edits in these files require careful `land.py` isolation.

**Verify:** Targeted board protocol and board model tests, local build, and an isolated-profile GUI capture of live suggestions and pre-save review.

## Tests
`ctest -R board`
`tests/test_board_protocol.py::WriteTests::test_triage_is_read_only_and_create_writes_only_selected_related_ids`
`tests/test_board_protocol.py::WriteTests::test_triage_with_no_provider_falls_back_without_blocking`
`tests/test_board_triage.py::ParseTests::test_discards_invented_values_and_overlapping_links`
`tests/test_board_triage.py::ParseTests::test_one_no_tools_call_returns_validated_semantic_suggestions`
`manual: docs/qa_evidence/2026-09-23-5kmq/01-draft.png`

### Check 2026-09-23 15:03
- passed · ctest:board — ctest -R board passed for this revision on spark-dcc9, 2026-09-23T19:03:58Z
- passed · unittest:tests.test_board_protocol.WriteTests.test_triage_is_read_only_and_create_writes_only_selected_related_ids — tests/test_board_protocol.py::WriteTests::test_triage_is_read_only_and_create_writes_only_selected_related_ids passed for this revision on spark-dcc9, 2026-09-23T19:03:58Z
- passed · unittest:tests.test_board_protocol.WriteTests.test_triage_with_no_provider_falls_back_without_blocking — tests/test_board_protocol.py::WriteTests::test_triage_with_no_provider_falls_back_without_blocking passed for this revision on spark-dcc9, 2026-09-23T19:03:58Z
- passed · unittest:tests.test_board_triage.ParseTests.test_discards_invented_values_and_overlapping_links — tests/test_board_triage.py::ParseTests::test_discards_invented_values_and_overlapping_links passed for this revision on spark-dcc9, 2026-09-23T19:03:58Z
- passed · unittest:tests.test_board_triage.ParseTests.test_one_no_tools_call_returns_validated_semantic_suggestions — tests/test_board_triage.py::ParseTests::test_one_no_tools_call_returns_validated_semantic_suggestions passed for this revision on spark-dcc9, 2026-09-23T19:03:58Z
- not-applicable · manual:docs/qa_evidence/2026-09-23-5kmq/01-draft.png — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-5kmq/01-draft.png
- notice · ctest:board — ctest -R board is slow: p95 2.61 s, p50 2.56 s
history: thread
## Execution Summary
Commit `27f849b` adds a pre-save new-card draft. Debounced local matches appear as the title or issue changes; Enter opens review and requests a bounded chores-role semantic pass. The owner can inspect duplicate cards, check related links, and edit tab/labels before creating. Only checked related IDs reach `links.related`; stale replies are ignored and an unavailable model leaves entry usable.

The targeted Board Qt test, focused Python tests, and exact-tree `relay-board-tests` build passed. The repo-wide board format check still reports errors in unrelated cards and threads; the full test-target build is also blocked by an unrelated `conversations_test.cpp` signature mismatch on current main. Focused test evidence is in `## Tests`.

![Pre-save review with editable tab and one selected related link](docs/qa_evidence/2026-09-23-5kmq/01-draft.png)
