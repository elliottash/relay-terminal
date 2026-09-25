<!-- relay:entry 20260925T041721Z-dx author=agent kind=event model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
- ✦ agent created this card in Discussing · .board/features/2026-09-25-close-the-two-loops-a-failed-case-suggests-or-fi.md

<!-- relay:entry 20260925T041854Z-6j author=agent kind=event model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
- ✦ agent updated this card · waiting_on: (unset) → owner; component: (unset) → ["worker", "board"]; verify: (unset) → {"artifact": "code", "primary": "script", "also": ["ai-text"], "human": "optiona…; replaced `## Plan`

<!-- relay:entry 20260925T042039Z-hn author=agent kind=event model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T042102Z-qn author=agent kind=question model=claude-fable-5-1 pane=06d99724 turn=cf99c92a9b1e48559ab6b383c65134c7/ddd172bea8164d12a0ac6253a6fb899d -->
Three questions:
1. Default `on_fail` is `suggest` (a draft in the tool result, the agent files only when you say so). Recommended: yes — it honours "no single approach" without a card flood.
2. Ship `file` now (opt-in per skill, one open card per server) or hold it until the registry (#9FX8) shows fail streaks? Recommended: ship it opt-in now; the flood guard is what makes it safe, and a skill that drives an external UI is the case that wants it.
3. Add `server:` as a front-matter field on work cards (the card that builds or fixes a server names it)? Recommended: yes; it is what lets the skill page list its cards and lets the hint stop once a card exists.
