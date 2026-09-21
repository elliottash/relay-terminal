<!-- relay:entry 20260921T150008Z-01 author=codex kind=progress -->
### Codex · 2026-09-21 15:00
Filed and claimed; fixing wrapped link hover ranges in TerminalView and checking rendering.

<!-- relay:entry 20260921T150127Z-m2 author=agent kind=event model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260921T150355Z-q5 author=agent kind=event model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260921T150355Z-xc author=agent kind=event model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260921T150355Z-xd author=agent kind=event model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260921T150407Z-1v author=agent kind=event model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
- ✦ agent moved this card · Executing → Needs verification · Wrapped-link hover rendering and targeted link tests pass under Xvfb; screenshots and QA checklist recorded. · evidence docs/qa_evidence/2026-09-21-wrapped-link-hover/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260921T150500Z-22 author=agent kind=event model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-21-wrapped-l… → {"plans": [], "commits": ["76091e05e8e2bdf9e0f3652b63dabeefb3e43c87"], "evidence…

<!-- relay:entry 20260921T150500Z-56 author=agent kind=evidence model=gpt-6-astra pane=2d18873c turn=ecdc6774405d4138a8c1054551ce1cbe/72d1e8e698ed4bca8c416cc1065437ff -->
Landed 76091e05 on main. The exact committed tree built relay-engine-tests successfully; its Xvfb run passed the new wrapped-hover regression and four existing link checks (7 passes including setup/cleanup). Screenshots and invocation are in docs/qa_evidence/2026-09-21-wrapped-link-hover/README.md.
