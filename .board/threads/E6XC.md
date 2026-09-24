<!-- relay:entry 20260919T200630Z-dp author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-19-board-update-card-can-never-match-sp4n-and-5py9.md

<!-- relay:entry 20260924T043022Z-q6 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043022Z-q7 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043333Z-60 author=agent kind=event model=kimi-k3 pane=switchboard turn=7aa3ea2fcc7271e03d268f8c72809d1e/03e0caf57d3349cdb24afbbf70213290 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043405Z-rx author=agent kind=event model=kimi-k3 pane=switchboard turn=7aa3ea2fcc7271e03d268f8c72809d1e/03e0caf57d3349cdb24afbbf70213290 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043421Z-8f author=agent kind=comment mode=plan model=kimi-k3 turn=7aa3ea2fcc7271e03d268f8c72809d1e/03e0caf57d3349cdb24afbbf70213290 -->
Let me check whether the bug still reproduces today by reading the two affected cards:





**Plan written on #E6XC.** The key finding from planning: the bug no longer reproduces — `file_hash()` (`backend/relay_core/board.py:1312`) is a plain sha256 hexdigest and can only ever return 64 chars, and a live `board_read` today shows #SP4N's hash as the card's 63-char string with its final `a` restored, i.e. the 2026-09-19 readings were off-by-one at the tail, pointing at agent transcription (or a since-fixed transport bug) rather than the hashing code.

The plan has the Run agent check the 2026-09-19 git history to settle which, apply the blocked label fixes to #5PY9 (`bug,gui`) and #SP4N (`+feature`) — which should simply work now — add a 64-hex regression test, and make the validation error self-diagnosing. Nothing needed from the owner; it's ready to Run.

<!-- relay:entry 20260924T043421Z-na author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260924T223838Z-e3 author=agent kind=event model=glm-5.3 pane=40bcc267 turn=b3c46c7f1ac9492083b3371d4688d8cd/0c818bb5ad25477184674801ffc07be7 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, session 40bcc267

<!-- relay:entry 20260924T223838Z-rk author=agent kind=progress model=glm-5.3 pane=40bcc267 turn=b3c46c7f1ac9492083b3371d4688d8cd/0c818bb5ad25477184674801ffc07be7 pane_token=40bcc267-38f3-4bf8-8da8-5987cc87fa4b -->
Claimed (40bcc267) · working on it from a terminal pane

Delivering per the card's plan: git-history check, the two blocked label fixes, 64-hex regression test, self-diagnosing base_hash error.

<!-- relay:entry 20260924T223845Z-3w author=agent kind=event model=glm-5.3 pane=40bcc267 turn=b3c46c7f1ac9492083b3371d4688d8cd/0c818bb5ad25477184674801ffc07be7 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "criteria…

<!-- relay:entry 20260924T224303Z-qk author=agent kind=event model=glm-5.3 pane=40bcc267 turn=b3c46c7f1ac9492083b3371d4688d8cd/0c818bb5ad25477184674801ffc07be7 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T224307Z-99 author=agent kind=event model=glm-5.3 pane=40bcc267 turn=b3c46c7f1ac9492083b3371d4688d8cd/0c818bb5ad25477184674801ffc07be7 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T224311Z-xy author=agent kind=event model=glm-5.3 pane=40bcc267 turn=b3c46c7f1ac9492083b3371d4688d8cd/0c818bb5ad25477184674801ffc07be7 -->
- ✦ agent moved this card · Executing → Needs verification · All four planned steps done: history check (transcription, not code), labels applied, regression test, self-diagnosing error; landed a14aca14d338, 312 tests OK. · implemented_by glm/glm-5.3
