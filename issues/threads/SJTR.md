<!-- relay:entry 20260922T020000Z-a1 author=claude-code kind=question -->
Two questions on the card, each with a recommendation: (1) scope now or hold as backlog — recommend hold; (2) first attacking agent should be code review or something new — recommend code review, since it already exists as a skill and gives an early read on the false-positive rate.

<!-- relay:entry 20260922T020000Z-a2 author=agent kind=progress model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
Owner requested: "lets discuss this card. can you do deep research on it and help me map it out". Researching methods, existing Relay/Provenance infrastructure and calibration, then producing a discussion map. This is research authorization, not authorization to implement or enable background attacks; keeping the feature in discussion.

<!-- relay:entry 20260922T020000Z-a3 author=agent kind=question model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
1. Is this a QA feature for any project opened in Relay, with Relay itself as the first proving ground, or just testing Relay itself? Recommendation: general project-facing design, narrow Relay pilot; research proceeds under that assumption pending owner input.

<!-- relay:entry 20260922T020000Z-a4 author=agent kind=event model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260922T020000Z-a5 author=agent kind=evidence model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
Three research passes complete: methods, existing infrastructure, calibration. Added Planning notes distinguishing verified reuse from missing infrastructure and correcting premises about mutation cost, W7 results, reviewer diversity and installed review availability. Report synthesis in progress.

<!-- relay:entry 20260922T020000Z-a6 author=agent kind=question model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
2. Should the first pilot combine evidence-bearing review of a frozen change with one executable harness, or ship review-only? Recommendation: combine them in a bounded pilot, so we evaluate actual reproducibility rather than only persuasive review text.
3. Should early findings remain advisory, with manual Make a card and no automatic repair/gating? Recommendation: yes; revisit only after held-out and ordinary-change calibration shows useful incremental discoveries at acceptable triage cost.

<!-- relay:entry 20260922T020000Z-a7 author=agent kind=event model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["YZ8G", "SW1D"], "githu… → {"plans": [], "commits": [], "evidence": ["reports/Relay QA attack system.md", "…

<!-- relay:entry 20260922T020000Z-a8 author=agent kind=evidence model=gpt-6-astra pane=73b4c2b2 turn=a502516a07124653b8b2fd982852128d/1931a6f54c874fb89172ec6cf6cef505 -->
Research complete: reports/Relay QA attack system.md, with three supporting research notes and primary citations. Checked report/local source references and spot-checked primary sources. No code, runtime tests or attacks performed. Board validator exits 1 on 13 existing errors elsewhere and existing section warnings; #SJTR has only its three pre-existing nonstandard-heading warnings. Feature remains discussing for owner scope decisions.
