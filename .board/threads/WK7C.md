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
