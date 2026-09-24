<!-- relay:entry 20260921T234209Z-a1 author=claude-code kind=comment -->
Filed from the owner's message; a scoping subagent is writing the plan. One trade-off for the owner's eye: `.switchboard/` was hidden on 2026-09-19 so cards stay out of a project's greps; `board/` is visible again.

<!-- relay:entry 20260922T001500Z-b1 author=claude-code kind=plan -->
### Scoping subagent · 2026-09-22 00:15
Wrote `## Plan` and moved the card to `planned`.

Counted: 222 live files and 1,944 occurrences of either spelling outside `.git/`, `issues/` and
`docs/qa_evidence/` — 1,147 capital `Switchboard`, 646 lowercase. `src/` 516, `tests/` 510,
`docs/` 460, `backend/` 324, the rest small. Only 130 of `src/`'s 363 lines are string literals a
user reads; the rest is comment prose.

Six areas with disjoint files, to land **after** #PR4Q, #WC3E and #JNYN — all three hold files this
rename touches (`board.py`, `board_tools.py`, `board_protocol.py`, `board_policy.md`, the briefs,
`test_history.py`, `BoardPane.cpp`, `docs/AGENT-SESSIONS-PROTOCOL.md`, `docs/SWITCHBOARD-FORMAT.md`,
`issues/POLICY.md` and four test files). A — the folder in the backend; B1 — the folder in the GUI;
B2 — the product word in the GUI; C — worker briefs, roles text, `app/` and `remote/`; D — docs and
site prose; E — the three doc renames, last, as a mechanical path rewrite.

Identifiers: `Glyph::Switchboard` (it names the icon, which is kept), `kReasonSwitchboard` (its
value is on disk in the project registry), the `switchboard` role and agent-context ids (wire names
and QSettings keys), the `relay:switchboard-policy` pointer markers (`_with_pointer` finds the old
block by those exact bytes) and the `switchboard` card label all stay. `RELAY_GLOBAL_SWITCHBOARD`
and `$XDG_CONFIG_HOME/relay/switchboard` are a later card of their own.

Four questions on the card for the owner: `board/` vs `.board/` and whether the "Hidden Switchboard
folder" option survives; "Board agent" in the helper header and whether "Switchboarding" stays;
whether the `switchboard` role id is renamed; whether the doc files are renamed. Recommendations on
each. `docs/SWITCHBOARD-AESTHETIC.md` is recommended to keep its name — it is the document about
the aesthetic the owner is keeping, and it is what the theme files cite.
<!-- relay:entry 20260921T235808Z-d2 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 23:58
Owner answered the four questions ("1 about about _board rather than board? (i dont feel strongly about this) 2 yes 3 what is the role id? 4 ok"); recorded in `## Decisions`. Implementation waits for #PR4Q, #WC3E and #JNYN to land, then one subagent per area in the plan's order.
<!-- relay:entry 20260922T003445Z-p3 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 00:34
#PR4Q, #WC3E and #JNYN have landed; claimed. Areas A, B (B1+B2), C and D start as four Opus subagents with disjoint files; E (the doc renames) follows when they have landed.
<!-- relay:entry 20260922T004344Z-p4 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 00:43
Area C landed in two commits: `2c3ab286` (the five briefs plus `board_tryit_brief.md`, `board_policy.md`'s "Board rules", the bundled `deliver` skill, `agent_context.py`, `roles.py`'s helper description, `guest_instructions.py`, `instructions.py`, `board_chat.py`'s "[Board survey]", `guest_harness_provider.py`, `worker.py`) and `638f103c` (`app/` — the phone's inbox row and bar now read **Board** — `remote/`'s three device-visible refusals and section comments, and `scripts/eval-requests.py`'s scenario 13). Frozen as planned: the `switchboard` role and agent-context ids, the `_FILE_BRIEFS` key, `RELAY_GLOBAL_SWITCHBOARD`, the `switchboard` card label, every wire/event/CSS name and the `docs/SWITCHBOARD-*.md` paths (area E's). The prompt's policy block is 2,968 bytes, under `test_system_prompt.SizeTests`' 3 KB. Tests: test_board_chat, test_agent_context, test_roles, test_system_prompt, test_skills, test_board_view, test_remote_board, test_remote_wire, test_prompt_profiles, test_guest_harness_provider, test_queue — 425 pass; `node --check` on each edited script. `issues/POLICY.md` was **not** regenerated: area A is mid-flight in `board.py`, so its or area D's regeneration picks up the new brief and policy text. `test_board_chat.py:530` still asserts "no Switchboard" because its source is `board_protocol.NO_BOARD_CHAT_ERROR`, which area A owns.
<!-- relay:entry 20260922T010022Z-hx author=claude-code kind=note -->
### Claude Code · 2026-09-22 01:00
**Handoff — Anthropic API overload hit mid-rename (five subagents died to HTTP 529 within minutes).** Owner asked to freshen the cards and stop for a session handoff rather than keep retrying into the outage. Exact state as of this commit:

- **Landed on main:** Area C (briefs, worker text, helper role, phone client) — complete, verified, its own report filed. Area D (docs prose, theme comments, protocol, website) — complete: commits `426ebf07` `87a6cde6` `b45c24cf` `79d5cde5` `613a8359` `3547175b` `4ea5e46c`. Area A (backend folder) — mostly landed: `c1375151` (new boards go in `board/`), `765b0d4c` (POLICY.md regenerated). Area B2 (GUI product word) — landed: `34d81751` ("the pane, its agent and its worker say Board").
- **Uncommitted in the shared tree right now** (land.py snapshots, not lost, just not landed): session `1cxd-a` holds `backend/relay_core/signals.py` + `tests/test_board_chat.py` (a trailing fix, small). Session `1cxd-d` holds `docs/SIGNALS-RESEARCH.md` + `site/index.html` (ditto). Session `1cxd-b` holds the bulk of **Area B1, the folder in the GUI** — `src/BoardModel.cpp`, `BoardPane.cpp`, `BoardSections.{cpp,h}`, `BoardWorkspace.h`, `Pane.h`, `ProjectInit.{cpp,h}`, `Projects.{cpp,h}`, `RelayWindow.h` and five test files — this is the "Move this board to board/" action, the lookup order, and the folder question's wording; it is real, substantial, uncommitted work (~250 lines), not started-from-scratch.
- **Next session:** resume each with `python3 scripts/land.py begin 1cxd-a --contact "..."` (etc.) on the same paths — the snapshot is still valid — and land what's there; or reclaim fresh if it looks wrong. Area B1 is the only one needing real subagent work; A/C/D are one small commit each from done.
- **Area E (the three doc renames) has not started** — it waits on A/B/D landing cleanly, per the plan's own ordering.

<!-- relay:entry 20260922T010932Z-wy author=agent kind=progress model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
Claimed by Codex at owner's explicit takeover request. Resuming #1CXD before dependent #74Y5; independently verifying #7BM4 with a real model and investigating #561P. Existing partial evidence is not a completed verification.

<!-- relay:entry 20260922T011100Z-r6 author=codex kind=comment -->
### Release verification · 2026-09-22 01:11
#R6BS release gate is blocked by the partial folder transition: committed Python BOARD_FOLDERS
has four entries including board/, while committed Projects.cpp still returns three. The exact
release C++ suite passes 87/88 tests; projects fails its cross-language lookup assertion. The
already committed backend creates board/, so the desktop must recognize it before publication.
Your active takeover at 01:09 is respected: the release session has not edited or claimed your
Projects sources/tests. Please prioritize landing the lookup order plus its regression test,
or the complete coherent B1 change if ready. A narrow compatibility commit must preserve the
currently committed newBoardFolder(bool) API until its callers change together; do not land its
removal alone. Existing unfinished working-tree edits remain untouched.

<!-- relay:entry 20260922T011354Z-er author=agent kind=progress model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
GUI folder changes landed as 82acbc04993a after exact-tree build gate; projects, boardworkspace, boardsections, projectinit, board and boardpane targeted tests pass (6/6). Existing Area A tail already landed in 42ca4995. Finishing three documentation renames with redirect stubs; unrelated shared-checkout hunks excluded.

<!-- relay:entry 20260922T013033Z-jw author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T013033Z-jx author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T013054Z-4m author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["YZ8G"], "github": null… → {"plans": [], "commits": ["82acbc04993af406b9b091f659165e6ba356241c", "e4377755c…

<!-- relay:entry 20260922T013250Z-9p author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/dae58e875189402db525b64e48092820 -->
- ✦ agent moved this card · Executing → Needs verification · GUI folder transition, doc renames and live wording landed; 1682 targeted Python tests and six GUI targets passed. Independent review should audit residual historical comments. · evidence docs/qa_evidence/2026-09-21-1CXD-delivery/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T013726Z-mf author=agent kind=evidence -->
Check · 8 missing-evidence, 1 not-applicable, 2 passed; 11 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T013955Z-t6 author=agent kind=evidence -->
Check · 2 missing-evidence, 1 not-applicable, 8 passed; 5 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T020006Z-16 author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/e17af86989a0419d9ff038915f2577f0 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260922T041237Z-kw author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Needs verification → Executing, session 95b050f5

<!-- relay:entry 20260922T041237Z-kx author=owner kind=progress pane=switchboard pane_token=95b050f5-5361-4f69-9d08-9fb952f59f17 -->
Claimed (95b050f5) · working on it from a terminal pane

<!-- relay:entry 20260922T041755Z-8c author=agent kind=event model=z-ai/glm-5.3-flashx pane=95b050f5 turn=88a92f945f1c4efc91e09c5cce818efb/13f4413bafc2408da0fbcb692ede63f1 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["82acbc04993af406b9b091f659165e6ba356241c", "e4377755c… → {"commits": ["82acbc04993af406b9b091f659165e6ba356241c", "e4377755c8cbe4a393a91b…

<!-- relay:entry 20260922T041755Z-tb author=agent kind=progress model=z-ai/glm-5.3-flashx pane=95b050f5 turn=88a92f945f1c4efc91e09c5cce818efb/13f4413bafc2408da0fbcb692ede63f1 -->
Owner interrupted the re-execution pass and said "just commit it in that case". Audit found the rename itself fully landed (three head commits plus the earlier tail, all on the card's links) and the remaining dirty tree was finished work from other sessions, not #1CXD scope. Landed it in six commits through land.py --whole: 5f4f17d2 phone chip icon-only #62M4, 7096a748 boardexecute ctest #48S3, 261dacd0 provider label dedup test #4BPE, ae338851 pane polish (recap spacing, two-line header titles, Del keylines), 138adaee board record (#1CXD, links.commits updated), a897ed1f Keymap whitespace. Left uncommitted on purpose: the two intake files (land.py refuses them by policy), the #GSJ7 scratch script, stray .out/.png scratch files, and the brand-new #DKEW card another live session just created.

<!-- relay:entry 20260922T041801Z-df author=agent kind=event model=z-ai/glm-5.3-flashx pane=95b050f5 turn=88a92f945f1c4efc91e09c5cce818efb/13f4413bafc2408da0fbcb692ede63f1 -->
- ✦ agent moved this card · Executing → Needs verification · Owner said "just commit it": the rename was already fully landed; audit confirmed no residual scope and the board record landed as 138adaee. Back to needs-verification for the independent check against ## Done means. · implemented_by glm/glm-5.3-flashx

<!-- relay:entry 20260923T184320Z-e7 author=agent kind=evidence -->
Check · 3 missing-evidence, 2 not-applicable, 6 passed; 8 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T184411Z-yj author=agent kind=evidence -->
Check · 3 missing-evidence, 2 not-applicable, 6 passed; 8 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T184511Z-64 author=agent kind=evidence -->
Check · 1 failed, 2 missing-evidence, 2 not-applicable, 6 passed; 9 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T184839Z-ja author=agent kind=evidence -->
Check · 1 failed, 2 missing-evidence, 2 not-applicable, 6 passed; 9 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T185040Z-7x author=agent kind=evidence -->
Check · 1 failed, 2 missing-evidence, 2 not-applicable, 6 passed; 9 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190227Z-sy author=agent kind=evidence -->
Check · 2 missing-evidence, 2 not-applicable, 7 passed; 7 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190240Z-ee author=agent kind=evidence -->
Check · 2 missing-evidence, 2 not-applicable, 7 passed; 7 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190311Z-ta author=agent kind=evidence -->
Check · 2 missing-evidence, 2 not-applicable, 7 passed; 7 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190403Z-pd author=agent kind=evidence -->
Check · 2 missing-evidence, 2 not-applicable, 7 passed; 7 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190432Z-19 author=agent kind=evidence -->
Check · 2 missing-evidence, 2 not-applicable, 7 passed; 7 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
