---
id: GREM
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: kimi/k3
session: 789000ea-27a3-4f62-aac8-d839e2bf2387
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [a0041445b6d3], evidence: [docs/qa_evidence/2026-09-25-grem-merge-tool/], related: [P4XN, BCJF], github: null}
---
# Offer board_merge_cards to pane agents, not only Board cleanup

## Issue
board_merge_cards exists but is cleanup-only: a terminal pane agent delivering #BCJF could not fold duplicate #P4XN into it, while the planner on the Board page (console scope) has the tool and wrote it into the plan. Owner decision: offer board_merge_cards in the ordinary pane tool set (guests get parity automatically via guest_board_bridge, which mirrors tool_specs). board_split_card and board_sections stay cleanup-only.

> "the agent says: Needs you: I couldn't merge the duplicate card #P4XN into #BCJF because I don't have a tool to merge cards. [...] can we check, is that a gap that needs to be feilled in the tooling?" — "i think we need to add that tool"
> — elliott · [session:092de508d8e544e4bc77246b0d158157](relay://session/092de508d8e544e4bc77246b0d158157) · 2026-09-25

## Done means
1. A terminal pane agent's offered board tools include `board_merge_cards`; calling it from a pane is not refused with the cleanup-only error. `board_split_card` and `board_sections` remain cleanup-only.
2. Guest sessions get it too (guest_board_bridge mirrors the native pane list), with tests asserting both.
3. Docs (`docs/AGENT-SESSIONS-PROTOCOL.md` 19.9 and scope table, `docs/ARCHITECTURE.md`) no longer describe merge as cleanup-only.
Failure looks like: a pane agent repeating #BCJF's 'no tool to merge cards' outcome, or split/sections leaking into ordinary turns.

## Tests
`python3 -m unittest tests.test_board_tools tests.test_board_chat tests.test_guest_board_bridge` with `PYTHONPATH=backend` and the `scripts/test.sh` isolation env: 379 tests, the only two failures are pre-existing at clean HEAD (filed #KZHX, verified on a `git archive` export of 3512773d): `test_native_catalog_parity_and_relay_policy_dispatch` (scratch tools not bridged to guests) and `test_the_same_tab_gets_its_conversation_back_and_another_tab_does_not` (board digest leak).

New/changed assertions all pass: `CleanupToolTests.test_a_pane_merges_a_duplicate_without_a_cleanup` (a pane merges, source becomes `dropped`, nothing deleted, split still unoffered), `test_they_are_refused_and_unadvertised_outside_a_cleanup` (now split+sections only), `SpecTests.test_the_designed_tools_are_offered_and_nothing_else`, `WorkerConsoleTest.test_a_terminal_panes_board_tools_are_untouched`, guest bridge pane `tools/list` includes `board_merge_cards` and still excludes split/`search_files`.

## Execution Summary
Commit a0041445b6d3 on main. `board_merge_cards` moved from `CLEANUP_TOOL_SPECS` into the ordinary `TOOL_SPECS` in backend/relay_core/board_tools.py: every pane agent is offered it every turn, the `run()` cleanup fence no longer names it (split/sections still refuse outside a cleanup), and the guest bridge mirrors `tool_specs()`, so guest sessions gain it with native parity — the thing #P4XN's pane could not do. Comments updated where they said 'the three cleanup tools'; docs updated (AGENT-SESSIONS-PROTOCOL 19.9 and the console-tools bullet, ARCHITECTURE scope table). Tests: new pane-merge test plus the three updated expectations. The policy's lightweight path (a duplicate closes with `resolution: duplicate`) was already available to panes; this adds the real fold.

## Try it
Open `docs/qa_evidence/2026-09-25-tryit-GREM/stage.sh` (run `bash docs/qa_evidence/2026-09-25-tryit-GREM/stage.sh`).

It rebuilds a two-card board in a scratch dir and plays the #BCJF moment as a terminal pane agent: the tool list it is offered, and the fold of the duplicate card into the one carrying the work. Read what it prints — one screen, no setup.

You are the pane's owner: is that fold the outcome you wanted handed to agents mid-delivery — duplicate gone, its words kept, nothing deleted — and would you let an agent do this to your board unwatched? (~2 min)

Expected: docs/qa_evidence/2026-09-25-tryit-GREM/expected.md (sealed until you answer)
