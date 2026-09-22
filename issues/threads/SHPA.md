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

<!-- relay:entry 20260922T133747Z-3g author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/d1bf228b2ea04e52b96078c50da5f5b5 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-ssh-parit… → {"plans": [], "commits": ["3d9e5538ef60f7641dc17fd0b8265bdf9d619119"], "evidence…

<!-- relay:entry 20260922T134240Z-da author=agent kind=evidence model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/c2362015864d427299da178d0d030bd9 -->
Owner follow-up: "and look here: 7c0c259171d44c7cb4a43bb113a34d08 the agent couldnt run commands in the ssh terminal?" Inspected saved Relay session and its linked Codex rollout 01a0c94c-cfb5-71f2-a4cd-6f483c56739d. Context identified filly, remote cwd /home/elliott, remote at prompt and shareable connection, and instructed run_command(host="filly"). Actual guest tool discovery found only nine relay_board board/delegation/todo tools; no Relay run_command/read_file/list_directory. guest_board_bridge.py:27-31 restricts ALLOW to BOARD_ALLOW | DELEGATION_ALLOW; specs filters to that set. No remote command was attempted; worker tools=0, guest only performed discovery. Root cause: native SSH tool instructions leak into guest context without exposing matching remote execution tools. Also direct visible-terminal access was withheld in context. Priority P1: expose session-bound remote command/file tools and optional terminal handoff to guest harnesses, and generate capability-accurate context. Merely telling the model to use SSH cannot repair this missing bridge.

<!-- relay:entry 20260922T142140Z-ez author=agent kind=note model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/0b10d5196d094424b34324653d4393bb -->
Owner: "great, add these issues to a card or cards so we can then deliver them". Created three unassigned inbox delivery cards with evidence, acceptance criteria, scoped tasks and related links: #S7GX guest SSH command/file tools and permitted terminal handoff; #S7LC remote command lifecycle, queues, spacing, activity and capability detection; #S7CX remote cwd/completion/attachments and nested-host identity. Suggested delivery order S7GX, S7LC, S7CX, with host identity guards coordinated early. These are implementation follow-ups; SHPA remains the audit record. No production work started.

<!-- relay:entry 20260922T142213Z-ht author=agent kind=note model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/0b10d5196d094424b34324653d4393bb -->
Corrected the newly created lifecycle card's identifier to #S7KC to satisfy the board's ID alphabet. Final delivery cards: #S7GX (guest SSH tools), #S7KC (command lifecycle/presentation), #S7CX (remote filesystem/host identity). All three are unassigned inbox cards.
