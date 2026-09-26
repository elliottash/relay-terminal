<!-- relay:entry 20260926T022109Z-0k author=agent kind=event model=gpt-6-astra pane=2e8d13e7 turn=dbc9af56def4427c91f4fecd2d277da4/4b4582c3fc9b489aa43a937e0ded95f7 -->
- ✦ agent created this card in Planned · .board/features/2026-09-25-b1-wire-integration-coordinator-and-sole-publica.md

<!-- relay:entry 20260926T022427Z-cd author=agent kind=event mention=3MH4 model=gpt-6-astra pane=2e8d13e7 turn=dbc9af56def4427c91f4fecd2d277da4/4b4582c3fc9b489aa43a937e0ded95f7 -->
mentioned in #3MH4 · 2026-09-26 · agent

<!-- relay:entry 20260926T025238Z-3y author=claude-code kind=progress -->
claimed this card; implementing B1 (`backend/relay_core/integration_service.py`, `landq.py`/`relay-land` service verbs, narrow `land.py` guards/hook, `agent._sync_board_writes`, `tests/test_integration_service.py`, `tests/test_integration_transitions.py`); wrote Done means, verify and Plan on the card

<!-- relay:entry 20260926T031933Z-1x author=claude-code kind=progress -->
landed 792cf7b18fcb (integration_service.py, landq/relay-land service verbs, land.py guards + hook v2, agent board-sync cwd, two test files; 20 + 13 new tests pass, 46 landq/flow and 149 land.py tests still pass, also on a clean export) and the evidence notes; moved the card to needs-verification with Execution Summary, Tests and Try it. The real repository was not activated.

<!-- relay:entry 20260926T033311Z-sf author=claude-code kind=progress -->
landed c459f5d603d2: the parent's review items 1-7 — fail-closed admission (gates, try, main build), ticks under the shared transition lock and capture-policy exclusive, main build under admission in a child process with RELAY_JOBS plus startup/lost-request/failed-build retry, ignored files cleaned between candidates with an external RELAY_BUILD_DIR, thread bytes append-only and no thread deletion plus the status enum, corrupt/unknown marker fails closed in both publishers with fcntl guarded, `relay-land submit REF` resolving in the caller's tree with the workspace/session recorded, and `accepted_config(repo, state_root)` for B2. 26 + 17 + 47 + 5 tests pass on a clean export; 149 land.py tests pass. B2's uncommitted `wake_reason` hunks in integration_service.py were left in the working tree for their session.
