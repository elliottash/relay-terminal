<!-- relay:entry 20260925T041729Z-s4 author=agent kind=event model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
- ✦ agent created this card in Discussing · .board/design/2026-09-25-name-the-other-objects-counterparty-on-cards-and.md

<!-- relay:entry 20260925T041952Z-pw author=agent kind=event model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
- ✦ agent updated this card · waiting_on: (unset) → owner; component: (unset) → ["worker", "board", "plugins"]; verify: (unset) → {"artifact": "system", "primary": "script", "also": ["probe", "person"], "human"…; replaced `## Plan`

<!-- relay:entry 20260925T042048Z-e8 author=agent kind=event model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T042110Z-zq author=agent kind=question model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
Three questions:
1. Which pilot first: Referee-Work (cases, skills, confidentiality, levels; needs only step 1 and hand-written rows) or outcome_test / soundmatch (runs, provenance; needs step 2)? Recommended: Referee-Work first, this week, since it proves the case ledger on real non-software work with no new code; the run ledger pilot follows step 2. Both are your trees, so this needs your go.
2. Does the run ledger belong on #MEPR's task-plugin card rather than here, since the plugin runner is the writer? Recommended: the ledger format lives here (it is an object of the data model), the plugin's writes land on #MEPR.
3. Counterparty as free text on the card, or as the id of a `contact` memory card so the same journal or client is one record? Recommended: free text now, a memory-card id accepted as well; a registry of contacts is a later object.

<!-- relay:entry 20260925T154831Z-cp author=agent kind=note model=claude-fable-5-1 pane=af0737e5 turn=068046613259444ba9b13f19ad048b93/ee5a47a968fd41cea78304e2ec96428d -->
From #EA37's research (`docs/research/project-board/b-agent-era-project-state.md`, Sacred's `run.json`): the run row in step 2 should record `dirty: bool` beside `code_hash` — whether the tree had uncommitted changes when the run started. A run from a dirty tree is the one that cannot be reproduced and no other ledger flags it. Step 3's `run:<id>#<path>` artifact address is confirmed as the design's address form (`docs/PROJECT-BOARD-DESIGN.md` §4.1); `superseded_by` is the version chain under a Canonical pointer written to the card's `links.artifacts` (§5). A run row is never deleted, only superseded (W&B's documented cascade-delete trap).
