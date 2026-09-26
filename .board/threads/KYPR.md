<!-- relay:entry 20260923T003653Z-ky author=codex kind=note -->
### Codex · 2026-09-23 00:36 UTC
Filed the focus-precedence and F1 pass-through findings at the owner's request. Evidence is source-level; competing handlers require GUI reproduction before claiming observed runtime failure. Included the owner's Ctrl/Ctrl+Shift consistency constraint; no keys changed.
<!-- relay:entry 20260923T012533Z-k3 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:25 UTC
Owner, 2026-09-22: "ctrl z is undo text edit; ctrl shift z is undo close. x and c reserved for cut and copy." That settles the Ctrl versus Ctrl+Shift question this card left open: Ctrl+Shift+key is Relay's layer; plain Ctrl+key is the same Relay command only where the key has no editing or terminal meaning (W, E, N, T, Q in the prompt box) and is otherwise left to the editor or program (A, S, Z, X, C, D, P); no Relay command differs between the two. The one pair that differs today, Ctrl+H / Ctrl+Shift+H, is queued as a question on #QWAS, which carries the full map.

<!-- relay:entry 20260923T024959Z-f1 author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent moved this card · Inbox → Executing · Part of the keyboard set (F1, Ctrl+N, Alt+Up precedence); owner said "implement it as you say now". · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260923T033526Z-bw author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260923T104412Z-4r author=agent kind=event model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["RBVK", "D60R", "T9ZS",… → {"plans": [], "commits": ["e914d65c", "cd7dcfcb", "c00a2859", "ea943915"], "evid…; appended to `## Execution Summary`

<!-- relay:entry 20260923T104418Z-kp author=agent kind=event model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ agent moved this card · Executing → Needs verification · Implementation, targeted tests and live keyboard evidence are recorded on this card; ready for a separate verifier. · evidence docs/qa_evidence/2026-09-22-keyboard-set/ · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260924T032313Z-ey author=agent kind=evidence -->
Check · 2 missing-evidence, 1 not-applicable, 1 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260924T193548Z-py author=agent kind=evidence -->
Check · 1 missing-evidence, 1 not-applicable, 2 passed; 2 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260926T005547Z-kx author=agent kind=evidence -->
Check · 1 failed, 1 missing-evidence, 1 not-applicable, 1 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260926T010025Z-gc author=agent kind=evidence -->
Check · 1 failed, 1 missing-evidence, 1 not-applicable, 1 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260926T010130Z-ca author=agent kind=evidence -->
Check · 1 failed, 1 missing-evidence, 1 not-applicable, 1 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
