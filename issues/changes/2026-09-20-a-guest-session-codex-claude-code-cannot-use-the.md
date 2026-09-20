---
id: 4NXH
type: work
status: discussing
labels: [bug, switchboard, guest]
waiting_on: owner
rank: zzzzzzzzzzzzzzi
created: '2026-09-20'
source: 'card #1V4F (Switchboard agent, 2026-09-20)'
links: {plans: [], commits: [], evidence: [], related: [1V4F, GT7X], github: null}
---
# A guest session (Codex/Claude Code) cannot use the Switchboard's board_* tools

## Issue
couldnt access tools:
The Switchboard board_* tools were not available in this session, so I could not append commit links or move #VWSD to needs-verification.
session: bb2d13c855b94b2c87c91ced1e437da6

## Findings
This is not a code regression: **a guest has no `board_*` tools by design today**, so the observed behaviour is the design showing.

- `docs/SWITCHBOARD-DESIGN.md` §4.10: *"A guest has no `board_*` tools on either route, so the brief says where the card and its thread live and what to do without them."* The Verify brief still names `board_update_card` / `board_move_card` for the verdict, which is what the session in this Issue hit.
- The board tools live in `backend/relay_core/board_tools.py` and are reached through the worker's own agent loop (`Agent.ask`); a guest harness turn never gets them — the protocol says Relay's tools are not offered to a guest ("it has its own").
- A guest CLI writing to a card is, today, only the `implemented_by`/`verified_by` argument the guest types (`board_tools.py:242`, "a guest CLI writing through the bridge") — there is no tool call, and the worker cannot stamp for it.
- The one bridge that exists, `backend/relay_core/guest_bridge.py` (protocol 26.5, Claude only), carries the IDE tools (`openDiff`, …) and no board tools. Generic MCP is #SSRQ, still deferred.
- So the fix is either to give the guest the board tools (an MCP surface the harness hands to Codex/Claude Code, guardrails unchanged) or to stop the briefs asking a guest for board calls and have Relay do the writes. That is the question below.
