<!-- relay:entry 20260925T001847Z-63 author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent created this card in Planning · .board/features/2026-09-24-give-agents-tools-to-change-per-job-model-overri.md

<!-- relay:entry 20260925T002014Z-8g author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · appended to `## Issue`

<!-- relay:entry 20260925T002024Z-4z author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260925T002031Z-sg author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260925T002042Z-8d author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260925T002046Z-wg author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · tasks: 0/5 done

<!-- relay:entry 20260925T002101Z-kf author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "system", "primary": "script", "also": [], "human": "none", "criter…

<!-- relay:entry 20260925T002109Z-sf author=agent kind=question model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
Shape of the agent surface — rows in the options catalog (recommended) or a dedicated `role` command?

**Rows** (recommended): one `roles.<job>` row per job in Options › Models under a collapsible "per-job models" heading. Agents reach it through `app_option_set`; "Agent changed <row>" notification with Undo, the agent-changed marker and the change list all come free, and §30.2's "the row catalog is the agent's settings surface" stays true. The Jobs tab stays the rich view. Cost: the rows are visible in Options too (they duplicate the Jobs tab's purpose in a minimal form).

**Command**: a `role` app command (list/set/clear, shaped like `reminder`) writing rolestore directly. Nothing changes in Options; notification/undo are wired by hand. Diverges from §30.2.

Either way: v1 covers tier-follow and preset|model pins; ranked candidate lists stay Jobs-tab-only, and the pane-opening action `agent.modelRoles` stays `agent_safe: false` (modal-pass question, out of scope).

<!-- relay:entry 20260925T002112Z-wh author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent moved this card · Planning → Discussing · Design question posted (rows vs command); waiting on the owner before implementing.

<!-- relay:entry 20260925T002115Z-r7 author=agent kind=event model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/558f83a8cb4d4d4bb9b2ffed04af56f3 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260925T002924Z-hf author=agent kind=decision model=glm-5.3 pane=a3b35894 turn=4355b5a90b9542f3becd4e12df568d03/0964214bbaf64674bf36d66109703584 -->
Owner, 2026-09-24: "i think by default agents, should be able to control relay -- options, actions, etc"

Consequences: options value rows are already default-settable (minus secrets), so this settles #WK7C's shape question — **rows**, per the recommendation. The actions side (default-off opt-in table) is the real flip and is filed as its own card, related to this one.

<!-- relay:entry 20260925T124937Z-ps author=owner kind=comment mode=discuss -->
Is this stale now?

<!-- relay:entry 20260925T125111Z-cb author=agent kind=event model=glm-5.3 pane=switchboard turn=730f4c52b11e8cf6691f18311fadf0e5/8791a7606c6d4d1980217c41f4a36e33 -->
- ✦ agent updated this card · waiting_on: owner → (unset); verify: {"artifact": "system", "primary": "script", "also": [], "human": "none", "criter… → {"artifact": "system", "primary": "script", "also": [], "human": "none", "criter…; appended to `## Planning notes`; tasks: 1/5 done

<!-- relay:entry 20260925T125119Z-sp author=agent kind=event model=glm-5.3 pane=switchboard turn=730f4c52b11e8cf6691f18311fadf0e5/8791a7606c6d4d1980217c41f4a36e33 -->
- ✦ agent moved this card · Discussing → Planned · Not stale: shape question settled by the owner's 2026-09-24 decision (rows); actions-side flip landed separately (#FRVM), but the per-job override rows still do not exist. Moving to planned, ready to build.

<!-- relay:entry 20260925T125132Z-0m author=agent kind=comment mode=discuss model=glm-5.3 turn=730f4c52b11e8cf6691f18311fadf0e5/8791a7606c6d4d1980217c41f4a36e33 -->
I'll check whether the card's work already exists in the code before answering.

The rows aren't in `RelayWindowModels.cpp` yet. Let me check whether the owner's "agents control relay by default" decision landed somewhere broader, which is what "stale?" likely refers to.



One more check — whether the per-job rows exist anywhere, and where `agent.modelRoles` now sits in the safety lists.

Not stale — but half its context changed. Let me record that on the card; reading it first for the hash.

**No — not stale, but its premise moved.** Recording the findings on the card:



**No — half of it is done, but the ask itself is still open.**

- **Done elsewhere:** the actions half. `actionIsAgentSafe` (src/AppCommands.cpp:266, #FRVM) is now default-on with named refusals — your "agents control relay by default" decision, landed. `agent.modelRoles` is even agent-safe now, but it still only *opens* the Jobs tab; it sets nothing.
- **Still missing:** the write path. There is no `roles.<job>` row anywhere in `src`, and `rolestore::setOverride` still lives only inside the Jobs tab widgets — so an agent today still cannot list or clear a per-job override through `app_option_set`. The `kimi`-pin incident would repeat unchanged.

On the card: appended a status check to `## Planning notes`, marked the shape task done (rows — settled by your decision), cleared `waiting_on`, and moved **#WK7C to planned**; also repaired its mangled `verify` block, which was failing validation. Ready to build — **Run** hands it to a terminal pane when you want it.

<!-- relay:entry 20260925T211245Z-qv author=agent kind=event mention=WBFM model=claude-opus-5-5 pane=1948e00e turn=6173581165764472b6c340786cfdcc26/82e12aaa9e394a2c85db01f3095e8848 -->
mentioned in #WBFM · 2026-09-25 · agent

<!-- relay:entry 20260926T090000Z-wk author=agent kind=event model=relay-subagent pane=164b2cbb -->
- ✦ agent claimed this card · planned → executing · landed together with #WBFM Track B (Jobs tab as global rules editor); claim wbfm-b covers src/JobsTab.cpp/.h, src/RelayWindowModels.cpp, backend/relay_core/roles.py, docs/AGENT-SESSIONS-PROTOCOL.md.

<!-- relay:entry 20260926T090000Z-wl author=owner kind=event pane=164b2cbb -->
Released (164b2cbb) · the pane closed

<!-- relay:entry 20260926T090000Z-wm author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · verify: {"artifact": "system", "primary": "script", "also": [], "human": "none", "criter… → {"artifact": "system", "primary": "script", "also": [], "human": "none", "criter…; appended to `## Tests`; tasks: 5/5 done

<!-- relay:entry 20260926T090000Z-wn author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260926T090000Z-wo author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260926T090000Z-wp author=agent kind=evidence -->
Check · 1 not-applicable; 2 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260926T090000Z-wq author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260926T090000Z-wr author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent moved this card · Running → Needs verification, wrote `## Execution Summary` · The per-job `roles.<job>` rows and shared rolestore writers are landed in #WBFM commit ff7c872. Targeted tests pass; the broader ModelsPane suite records only the existing #SYTR rename mismatch. · evidence docs/qa_evidence/2026-09-25-wbfm-b/ · implemented_by openai/gpt-6-luna via codex

<!-- relay:entry 20260926T090000Z-ws author=agent kind=evidence model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
Continuation verification at current shared-checkout HEAD d30cb637 (source includes #WBFM/#WK7C commit ff7c872db2e6): `scripts/relay-build --target relay-modelpicker-tests relay-modelspane-tests relay-jobstab-tests relay-appcommands-tests relay-settings-tests` passed. `ctest --test-dir build -R '^(modelpicker|modelspane|jobstab|appcommands|settings)$' --output-on-failure`: modelpicker, jobstab, appcommands passed; modelspane and settings each have one failure, both the already tracked #SYTR stale expectation `Helper Agent (Alt+Q)` vs current `Agent (Alt+Q)`. The #WK7C cases in jobstab/appcommands pass. This confirms the earlier `tests_run` output was on pre-Track-B revision 8659c576 but the remaining two failures are real at current HEAD and already tracked by #SYTR.
