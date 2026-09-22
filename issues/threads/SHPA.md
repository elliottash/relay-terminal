<!-- relay:entry 20260922T133151Z-a1 author=codex kind=progress -->
### Codex · 2026-09-22 13:31
Claimed this audit; tracing local/remote shell behavior, then live-driving localhost SSH in isolated Relay.

<!-- relay:entry 20260922T133202Z-3m author=agent kind=progress model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
Code confirms remote-integration.sh lacks local prompt/output spacing. refreshBusyLine uses processBusy()/foregroundProgramName(), so an idle SSH transport remains cyan. Auditing remote lifecycle and live-driving a real localhost connection next.

<!-- relay:entry 20260922T133419Z-vp author=agent kind=evidence model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
Real localhost GUI drive reproduced missing spacing and persistent cyan busy line with login_enhance/shared=1 logged. Remote cd /tmp leaves local cwd chip visible. A shell-forced echo while read waits becomes read's input (RECEIVED=echo SHOULD-BE-A-COMMAND). SSH host badge and return to local shell work. Existing targeted tests pass: 25 shell tests, 46 backend remote tests, three C++ tests. Evidence: docs/qa_evidence/2026-09-22-ssh-parity/.

<!-- relay:entry 20260922T133616Z-b4 author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["S5SH"], "github": null… → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-ssh-parit…; replaced `## Execution Summary`

<!-- relay:entry 20260922T133632Z-50 author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T133654Z-xb author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T133708Z-wr author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
- ✦ agent moved this card · Executing → Needs verification · SSH audit and real localhost GUI reproduction complete; production fixes are documented follow-up work. · evidence docs/qa_evidence/2026-09-22-ssh-parity/ · implemented_by openai/gpt-6-astra via codex
