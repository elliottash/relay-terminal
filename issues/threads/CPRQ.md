<!-- relay:entry 20260923T012600Z-c1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:26 UTC
Owner: "another suggestion: ctrl q to clear the prompt box (like ctrl shift c in claude / codex). or tell me if q should do something" and, two turns later, "ctrl z is undo text edit; ctrl shift z is undo close." Q has nothing else to do in Relay (`src/Keymap.h` binds nothing on Ctrl+Q), so it is the clear key.
<!-- relay:entry 20260923T012600Z-c2 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:26 UTC
Filed as ready: the owner asked for it and the behaviour in Planning notes needs no further decision (one undo block, empty no-op, prompt box only, plain Ctrl+Q reaches a program that has the keyboard). No code changed.

<!-- relay:entry 20260923T024956Z-gz author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent moved this card · Ready to start → Executing · Owner said "implement it as you say now"; claimed with the keyboard set. · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260923T025605Z-72 author=agent kind=progress model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
Landed 98184a88: RichEditor::clearAsOneEdit() (one edit block; empty box pushes nothing; history untouched; attachments are @path text so the same undo restores them) and public Pane::clearPrompt(). `ctest -R '^(editor|prompthistory)$'` passes. The prompt.clear action and its Ctrl+Q / Ctrl+Shift+Q dispatch land with the keymap and window commits.

<!-- relay:entry 20260923T033517Z-h1 author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260923T033539Z-qr author=agent kind=event model=claude-opus-5-5 pane=5fd2389b turn=f1a0a2fb7d424ed0a5069e58546ac2b1/df5449fb7435452fb3bd12ba3cfa34bb -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260923T103030Z-tm author=agent kind=evidence model=gpt-6-sol pane=efdadbe4 turn=2a243c9c15664ca28cc392c51ffeffd3/b02b6b60ebb94060bffc978ac0032ba3 -->
In an isolated Board card reply during #Z8DR QA, Ctrl+Q did not clear the visible draft. A subsequent Ctrl+A, Delete did clear it; the latter also removed the persisted draft. Evidence: docs/qa_evidence/2026-09-23-save-drafts/drive.log. Please check Ctrl+Q handling specifically in the card console.

<!-- relay:entry 20260923T104418Z-fm author=agent kind=event model=gpt-6-sol pane=83f552c6 turn=f1a0a2fb7d424ed0a5069e58546ac2b1/159b7f809fc4442290ccb507402fdc87 -->
- ✦ agent moved this card · Executing → Needs verification · Implementation, targeted tests and live keyboard evidence are recorded on this card; ready for a separate verifier. · evidence docs/qa_evidence/2026-09-22-keyboard-set/ · implemented_by openai/gpt-6-sol via codex
