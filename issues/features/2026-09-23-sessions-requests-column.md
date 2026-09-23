---
id: R4WX
type: work
status: needs-verification
labels: [feature, sessions, gui]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-open-requests-R4WX/], related: [EV45], github: null}
---
# Put unfinished requests in a sortable Sessions column

## Issue
also its not clear what "open" means in the turns column

move that into another column, requests, that i can also sort by. also i can only sort with the dropdown that says "newest first", i can sort by clicking the column headers

## Done means
- Sessions shows turn counts alone in Turns and unfinished user-request counts in a Requests column.
- Clicking Requests sorts by its count in both directions; the sort dropdown and arrow stay in sync.
- Header clicks work on the visible list, including paging and grouping, or the remaining UI limitation is made clear.

## Plan
**Goal.** Give outstanding requests their own sortable column and make header sorting usable.

**Findings.** `open_requests` is `RequestLedger.open_count()` (requests still open or in progress), stored in `backend/relay_core/conv_index.py`. `src/Conversations.cpp` currently appends it to Turns. Header clicks already map to worker sort IDs, while grouping can obscure global order.

**Steps.** Add request-count sort IDs to the worker; add the Requests column and click mapping; verify with targeted backend and Qt tests and an isolated GUI capture.

**Risks.** Several sessions hold `src/Conversations.cpp`; land only this change's hunks. Preserve other sessions' edits.

**Verify.** Targeted index and conversations tests, plus a captured Sessions list showing the new column.

## Execution Summary
Moved Relay's unfinished user-request count out of Turns into Requests. Guest, terminal and subagent rows show a dash because they have no Relay request ledger. Added most/fewest request sorting and made the Sessions header accept real mouse clicks. The screenshot shows the separate Turns and Requests columns: ![Sessions list with Requests column](docs/qa_evidence/2026-09-23-open-requests-R4WX/turns-cell.png)

## Tests
`python3 -m unittest tests.test_conv_index.IndexTests.test_shortest_requests_title_and_model_sorts` (pass)
`scripts/relay-build --target relay-conversations-tests` (pass)
`RELAY_SHOT_DIR=docs/qa_evidence/2026-09-23-open-requests-R4WX QT_QPA_PLATFORM=offscreen build/relay-conversations-tests managerGroupsByProjectAndSearches headerClickSortsByThatColumn sessionsTableShowsRecapAndPreviewsOnDemand` (pass: 5/5)
manual: `docs/qa_evidence/2026-09-23-open-requests-R4WX/turns-cell.png`
