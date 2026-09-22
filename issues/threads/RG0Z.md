<!-- relay:entry 20260921T122259Z-4z author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-21-add-a-hide-until-you-need-me-mode.md

<!-- relay:entry 20260921T185420Z-jx author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · labels: (unset) → ["feature", "panes", "appearance"]; appended to `## Issue`

<!-- relay:entry 20260921T185458Z-dw author=agent kind=progress model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
Claimed by codex via POLICY.md file fallback; implementing the agreed pane dimming controls in this checkout.

<!-- relay:entry 20260921T185458Z-vc author=agent kind=decision model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
Owner: "completion does not ovefrride manual dimming"; "otherwise i agree with your recs, put it in the card"; "we are out of plan mode, so you can write to the card and execute". Accepted: pane-local knobs, wheel targets hovered pane and keys active pane; up/+ brightens, down/− dims by 5%; Options default; temporary reveal on entering; explicit adjustment immediately dims even an active pane. Questions/blocked states override dimming without changing keyboard focus; completion restores automatic dimming but preserves manual dimming with a visible indicator.

<!-- relay:entry 20260921T185458Z-vd author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260921T185458Z-ve author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260921T190158Z-5s author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260921T190330Z-65 author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260921T190330Z-66 author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260921T190330Z-qk author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent moved this card · Executing → Needs verification · Implemented agreed dimming controls; app build and four targeted tests pass, with isolated Xvfb dark/light evidence. Ready for independent GUI verification. · evidence docs/qa_evidence/2026-09-21-pane-dimming/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260921T190509Z-5r author=agent kind=event model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-21-pane-dimm… → {"plans": [], "commits": ["a4c930d05b9964c05a7b7b932c736c6137785d4c"], "evidence…

<!-- relay:entry 20260921T190509Z-7q author=agent kind=evidence model=gpt-6-astra pane=9255636d turn=bde50672e69443a48e42855349d1893a/f33091886e6c4af78bad54a1ddf55a5f -->
Landed a4c930d05b9964c05a7b7b932c736c6137785d4c through scripts/land.py. The exact committed tree passed its isolated build gate. Four targeted CTest cases passed in the shared checkout; Xvfb evidence covers dark/light themes, focus dimming, Alt keys/wheel, keyboard focus retention and temporary reveal. Card remains needs-verification for independent live agent/GUI QA.

<!-- relay:entry 20260921T205648Z-k4 author=agent kind=note model=gpt-6-astra pane=8362237d turn=8348624294224e0d8d5b4bb26370542d/371c1ac186c144008e87f9fabe0a7dc3 -->
User reports: "can you find the card for the dimmer? alt mousewheel isnt working". Alt+mousewheel is not working in the user’s running app; the cause has not yet been investigated. This is covered by the existing Alt+wheel QA checklist item and remains unresolved.

<!-- relay:entry 20260921T210513Z-bb author=agent kind=decision model=gpt-6-astra pane=059ac420 turn=cc704b9809304a888913483e4eaae5aa/6b2aa065f6bf4d9ebcdae01e8918f46e -->
Owner: and change "hide until you need me" to "dim while working". Updated the Options and Actions labels and keybinding description in 617b7b68. Ctrl+Alt+D and behavior are unchanged.

<!-- relay:entry 20260921T212846Z-dw author=codex kind=progress -->
Claimed the Alt+wheel fix after owner request "fix that issue". Native Qt/X11 probe reproduces Alt converting vertical wheel deltas to the horizontal axis; Relay discards that component. Plan: accept either angle axis, add directional/partial-notch regression tests, run the targeted Qt test and native Xvfb probe, build and land. Board bridge unavailable this turn; using file fallback.

<!-- relay:entry 20260921T213136Z-dw author=codex kind=evidence -->
Alt+wheel fix landed as 5bac2a5d: accept the horizontal angle delta produced by Qt/X11 while Alt is held. Targeted regression and exact committed app build pass. Full app under isolated Xvfb: four native Alt+wheel-down events show the dim overlay; four up hide it. Added commit link, tests and wheel-fix.md evidence; returned to needs-verification. Shared app build remains blocked by concurrent model-picker changes. Thread entries left in working tree because landing only this append conflicts with pre-existing uncommitted entries; preserved all entries.
