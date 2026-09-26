<!-- relay:entry 20260926T124636Z-eq author=agent kind=event model=gpt-6-sol pane=6fa2d509 turn=15271cf0c35e43bc91433afe814b3a16/5b97164315034c1d8b55aaea126d3abd -->
- ✦ agent created this card in Planned · .board/features/2026-09-26-expose-curated-skill-libraries-and-open-discover.md

<!-- relay:entry 20260926T124655Z-ar author=agent kind=event model=gpt-6-sol pane=6fa2d509 turn=15271cf0c35e43bc91433afe814b3a16/5b97164315034c1d8b55aaea126d3abd -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260926T124718Z-mb author=agent kind=event model=gpt-6-sol pane=6fa2d509 turn=15271cf0c35e43bc91433afe814b3a16/5b97164315034c1d8b55aaea126d3abd -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260926T124726Z-fp author=agent kind=event model=gpt-6-sol pane=6fa2d509 turn=15271cf0c35e43bc91433afe814b3a16/5b97164315034c1d8b55aaea126d3abd -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "system", "primary": "script", "also": ["person"], "human": "option…

<!-- relay:entry 20260926T124738Z-17 author=landq kind=note -->
Landing job b95b8854a28acbf2 (3ec4c6d07ac9) landed.
Published 3ec4c6d07ac9 onto main (target was d13f251fc67f). <!-- landq:b95b8854a28acbf2:landed -->

<!-- relay:entry 20260926T125246Z-1r author=agent kind=event model=gpt-6-sol pane=6fa2d509 turn=15271cf0c35e43bc91433afe814b3a16/f8f1547025014dd3864ae9a1657f7e2f -->
- ✦ agent claimed this card · assignee agent, Planned → Running, implemented_by openai/gpt-6-sol via codex:elliott-t-ash-gmail-com, session 6fa2d509

<!-- relay:entry 20260926T125246Z-w8 author=agent kind=progress model=gpt-6-sol pane=6fa2d509 turn=15271cf0c35e43bc91433afe814b3a16/f8f1547025014dd3864ae9a1657f7e2f pane_token=6fa2d509-138c-4a64-8910-c6cb50031874 -->
Claimed (6fa2d509) · working on it from a terminal pane

Implement curated discovery and reviewable import in Globals › Skills; verify with targeted backend and UI tests.

<!-- relay:entry 20260926T125709Z-ky author=agent kind=event mention=K26R model=claude-opus-5-5 pane=5da13a9d turn=8867c19893f947f7ad05061ab27c6061/d054f37b5574495a85606e9b8eab2ea1 -->
mentioned in #K26R · 2026-09-26 · agent

<!-- relay:entry 20260926T125721Z-rt author=agent kind=event mention=G8JN model=claude-opus-5-5 pane=5da13a9d turn=8867c19893f947f7ad05061ab27c6061/d054f37b5574495a85606e9b8eab2ea1 -->
mentioned in #G8JN · 2026-09-26 · agent

<!-- relay:entry 20260926T125746Z-tj author=agent kind=event mention=4EMF model=claude-opus-5-5 pane=5da13a9d turn=8867c19893f947f7ad05061ab27c6061/d054f37b5574495a85606e9b8eab2ea1 -->
mentioned in #4EMF · 2026-09-26 · agent

<!-- relay:entry 20260926T125804Z-yn author=agent kind=event mention=M91Y model=claude-opus-5-5 pane=5da13a9d turn=8867c19893f947f7ad05061ab27c6061/d054f37b5574495a85606e9b8eab2ea1 -->
mentioned in #M91Y · 2026-09-26 · agent

<!-- relay:entry 20260926T125827Z-ft author=agent kind=note model=claude-opus-5-5 pane=5da13a9d turn=8867c19893f947f7ad05061ab27c6061/d054f37b5574495a85606e9b8eab2ea1 -->
Coordination from the #SZ1H orchestration (2026-09-26), so the sibling cards don't collide with this one while it is executing:
- **#K26R** adds worker row fields only: `availability {state: runnable|hidden|needs, reasons[]}`, `aliases[]`, `content_hash` on `list_skills` / `skills_registry`. It will **not** edit `src/SkillRegistryView.*`; rendering "works here / needs setup / unavailable here" from those fields is this card's.
- **#G8JN** owns the guest filter: `guest_instructions.build_instructions` gets the guest id and omits the harness's own home tree. This card's guest check can consume it rather than implement a second filter.
- **#M91Y** (waiting on owner) will supply semantic-review candidates as data; it reuses `import_skills_preview`/`confirm` rather than a parallel import path.
