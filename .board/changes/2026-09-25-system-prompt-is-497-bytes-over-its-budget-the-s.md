---
id: K54A
type: work
status: inbox
labels: [bug, prompt]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# System prompt is 497 bytes over its budget; the size test is red at HEAD

## Issue
tests.test_system_prompt.SizeTests.test_the_prompt_and_the_tools_stay_within_their_budgets fails at git HEAD 2f848bec with "system prompt grew to 7153 bytes" against the 6 KiB + 512 budget (6656). Reproduced on a clean copy of HEAD's board.py, board_tools.py, board_policy.md and tools.py: the number is identical (7153), so no uncommitted work causes it. Measured 2026-09-25 while landing #EMWF; the comment in the test says 5.9 KB was measured 2026-09-20 (#GMCF), so a landed change since then grew the boardless system prompt by ~500 bytes past its budget. Fix: find the growth (likely a prompt block added without the budget test run) and either trim it or make the next budget a stated decision.
