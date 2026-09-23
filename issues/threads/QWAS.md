<!-- relay:entry 20260923T012600Z-q1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:26 UTC
Owner, over three turns in this pane: "the most accessible keys are 1 2 3 q w e a s d z x c"; "i dont want a ctrl and ctrl shift to have different funcs"; "nobody has used relay yet so we can think fresh."; "qweasdzxc are really the sweet spot for accessibility"; "what if ctrl shift d is the file explorer. ctrl z is undo text edit; ctrl shift z is undo close. x and c reserved for cut and copy." The line on Z settles the modifier question that #KYPR left open: plain Ctrl stays the editor's where the key has an editing meaning.
<!-- relay:entry 20260923T012600Z-q2 author=claude-code kind=question -->
### Claude Code · 2026-09-23 01:26 UTC
1. The modifier rule as written in Planning notes: Ctrl+Shift+key is Relay's; plain Ctrl+key is the same Relay command only where the key has no editing or terminal meaning (W, E, N, T, Q in the prompt box), otherwise the editor's or the program's; never a different Relay command. Recommendation: yes, it is what your Z line implies, and it keeps Select All, Save, Undo, Cut, Copy, end-of-input and history working.
2. Ctrl+H / Ctrl+Shift+H are today the one letter pair with different commands (take control / back to the prompt). Make both one toggle? Recommendation: yes.
3. Globals become a tab inside Sessions & Projects and Ctrl+Shift+G is freed? Recommendation: yes, as #SPSG suggested.
<!-- relay:entry 20260923T012600Z-q3 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:26 UTC
Filed at the owner's "ready to put this on cards?". The map in Planning notes is a proposal read against `src/Keymap.h` at 275ac2ea; no bindings changed. Depends on #SPSG, #CPRQ and #MAGP landing first; #DKEW (Board on B) is superseded by A here.

<!-- relay:entry 20260923T024920Z-wt author=agent kind=decision model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
Owner, 2026-09-22: "implement it as you say now". Taken as yes to all three recommendations: the modifier rule as written, Ctrl+H / Ctrl+Shift+H become one toggle, Globals is a tab inside Sessions & Projects and Ctrl+Shift+G is freed.

<!-- relay:entry 20260923T024955Z-83 author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent moved this card · Discussing → Executing · Owner said "implement it as you say now"; claimed by Claude Code in this pane with MAGP, CPRQ, SPSG, ACDG and KYPR. · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260923T033511Z-4s author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent updated this card · waiting_on: owner → (unset); replaced `## Tests`

<!-- relay:entry 20260923T033533Z-26 author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260923T101943Z-as author=agent kind=decision model=openai/gpt-6-luna pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/0ba507b22fe14ae5b07e8e678cacc343 -->
"ctrl shift g can still be globals". Rebind Ctrl+Shift+G to `globals.open` in the Relay preset; keep plain Ctrl+G unbound, preserving the editor/program key.

<!-- relay:entry 20260923T101946Z-gv author=agent kind=event model=openai/gpt-6-luna pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/0ba507b22fe14ae5b07e8e678cacc343 -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260923T102002Z-4r author=agent kind=event model=openai/gpt-6-luna pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/0ba507b22fe14ae5b07e8e678cacc343 -->
- ✦ agent claimed this card · assignee agent, implemented_by openai/gpt-6-luna, session 83f552c6

<!-- relay:entry 20260923T102002Z-4s author=agent kind=progress model=openai/gpt-6-luna pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/0ba507b22fe14ae5b07e8e678cacc343 pane_token=83f552c6-372f-4f64-9b01-2beb4e493f9c -->
Claimed (83f552c6) · working on it from a terminal pane

Restore Ctrl+Shift+G as a direct Globals shortcut and update keymap docs and tests.

<!-- relay:entry 20260923T102428Z-dm author=agent kind=event model=openai/gpt-6-luna pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/0ba507b22fe14ae5b07e8e678cacc343 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260923T103220Z-td author=agent kind=decision model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
Owner correction: "ctrl shift s on sessions". Ctrl+Shift+P returns to Projects, Ctrl+Shift+S stays on Sessions, and Ctrl+? alone opens Actions/help.

<!-- relay:entry 20260923T103822Z-ms author=agent kind=event model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ agent updated this card · title: Adopt the QWEASDZXC default keymap: A Board, S Sessions & Projects, D explorer, … → Adopt the QWEASDZXC default keymap: Board, Sessions, Projects, explorer, Actions…; appended to `## Decisions`; replaced `## Done means`

<!-- relay:entry 20260923T103822Z-mt author=agent kind=rewrite model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ rewrote title

<details><summary>before</summary>

```
Adopt the QWEASDZXC default keymap: A Board, S Sessions & Projects, D explorer, P Actions
```

</details>

<details><summary>after</summary>

```
Adopt the QWEASDZXC default keymap: Board, Sessions, Projects, explorer, Actions/help
```

</details>

<!-- relay:entry 20260923T104340Z-qn author=agent kind=event model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["SPSG", "MAGP", "CPRQ",… → {"plans": [], "commits": ["e914d65c", "cd7dcfcb", "3c66dd01", "cbdd1166", "c00a2…; appended to `## Execution Summary`; replaced `## Tests`

<!-- relay:entry 20260923T104350Z-xd author=agent kind=event model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ agent moved this card · Executing → Needs verification · Revised shortcuts and Sessions layout landed in 878b0ca4 and 5df774b4; targeted Qt/Python tests passed, with Xvfb screenshots. · evidence docs/qa_evidence/2026-09-23-sessions-projects-keys/ · implemented_by openai/gpt-6-sol via codex
