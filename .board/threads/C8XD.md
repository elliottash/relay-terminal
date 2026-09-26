<!-- relay:entry 20260925T185222Z-hr author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/891b4f5955d34d3c91febc26675f7022 -->
- ✦ agent created this card in Discussing · .board/features/2026-09-25-init-creates-relay-md-as-the-default-agent-guida.md

<!-- relay:entry 20260925T185232Z-6x author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/891b4f5955d34d3c91febc26675f7022 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Planning notes`

<!-- relay:entry 20260925T185233Z-b1 author=agent kind=question model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/891b4f5955d34d3c91febc26675f7022 -->
Should the generated Board policy block **move out of CLAUDE.md/AGENTS.md into RELAY.md** (one source of truth; the other files keep only the generated `@RELAY.md` one-liner), or stay embedded in both places? I recommend moving it — the imports reach Claude Code and Codex either way, and the board policy is written once. (1 of 2)

<!-- relay:entry 20260925T185241Z-q3 author=agent kind=question model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/891b4f5955d34d3c91febc26675f7022 -->
When `/init` runs in a project that still has a **legacy `WARP.md`** (pre-#2M26 projects), should Relay rename it to `RELAY.md` as part of init — content untouched, just the file moved so the note points at the new name — or leave it and add the note to the old file? I recommend renaming: it is the migration this repo just did by hand, and `WARP.md` remains recognized either way. (2 of 2)

<!-- relay:entry 20260925T185659Z-fc author=agent kind=decision model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
"yes and no -- warp.md is the agents file that warp terminal uses" — (1) yes: the generated Board policy block moves out of CLAUDE.md/AGENTS.md into RELAY.md, which becomes the default agent guidance; (2) no: never rename a WARP.md — it is the Warp terminal's own agent instructions file, so Relay treats it as a first-class convention (like CLAUDE.md for Claude Code) and only adds the @RELAY.md note to it. This repo's own rename (#2M26) stands: here WARP.md was Relay's own working-rules file, misnamed.

<!-- relay:entry 20260925T185708Z-tv author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by glm/glm-5.3, session 0e858e88

<!-- relay:entry 20260925T185708Z-tw author=agent kind=progress model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 pane_token=0e858e88-8869-47e7-a7c2-16425f166d4d -->
Claimed (0e858e88) · working on it from a terminal pane

Decisions recorded; implementing the RELAY.md-first scaffold now.

<!-- relay:entry 20260925T185717Z-pp author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off…; waiting_on: owner → (unset); appended to `## Done means`

<!-- relay:entry 20260925T190626Z-e4 author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T190634Z-tp author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T190642Z-f7 author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
- ✦ agent moved this card · Executing → Needs verification · Landed as e7ce0f13 + 3cc4397e: RELAY.md created by /init with the Board policy block, @RELAY.md note in CLAUDE.md/AGENTS.md/WARP.md (never renamed), old blocks migrated, dogfooded on this repo, 197 tests pass. · evidence docs/qa_evidence/2026-09-25-c8xd-relay-md-default-guidance/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T190643Z-4w author=agent kind=progress model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/308974f1b9c5422db40ce81192bc0642 -->
Landed as e7ce0f13 (+3cc4397e card record) and dogfooded on this repo the same hour: this checkout's CLAUDE.md and AGENTS.md now hold only the @RELAY.md note, and RELAY.md carries the Board policy block. One honest revision to Done means: there is no `doctor` command in Relay's backend, so the missing-RELAY.md visibility is `instructions.load`'s `skipped` ("missing") list plus the next scaffold recreating the file. The verifying session should re-run the two test files and try `python3 scripts/relay-board.py --board .board policy` twice (second run must write nothing but POLICY.md, or nothing at all).

<!-- relay:entry 20260925T194227Z-mv author=agent kind=decision model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/2df649de5718493dbc889a56bbf8055e -->
"dont create agents or claude or warp when absen[t]" — init creates only `RELAY.md`; `CLAUDE.md`, `AGENTS.md` and `WARP.md` are annotated when they exist and never created. This supersedes the #R9G7 behavior of creating an `AGENTS.md` when the project has none. A project with no instruction file still gets its guidance: Relay's own discovery loads `RELAY.md` as a first hit.

<!-- relay:entry 20260925T194531Z-5x author=agent kind=event model=glm-5.3 pane=0e858e88 turn=9698842186c7475288ae58fe80f77b2e/2df649de5718493dbc889a56bbf8055e -->
- ✦ agent updated this card · appended to `## Tests`
