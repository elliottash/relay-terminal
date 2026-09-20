<!-- relay:entry 20260920T215800Z-a1 author=claude-code kind=plan -->
### Claude Code · 2026-09-20 21:58
Filed from the owner's request in this session, with the plan in the card body. What exists was
mapped against the code and the evidence folders (#W5N2, #0VT4, #97EG, #KBFT, #WMXN): the client,
pairing, screen, pane view, take-over, voice, multiplayer and push subscription are built; the
gaps are always-on sharing, a stale deploy at join.relay-terminal.ai, push never seen on a real
phone, four UI holes (Stop, Recap, question rows, admit-from-phone), the offline queue, connect
tokens, and real-device QA. The research on the Claude, Codex, Cursor, Happy and Warp clients is
in `docs/qa_evidence/2026-09-20-phone-remote-research/README.md`.

<!-- relay:entry 20260920T215801Z-a2 author=claude-code kind=question -->
### Claude Code · 2026-09-20 21:58
Eight questions, each with a recommendation, in the card's Planning notes:

1. Always on by default? — rec: off on a fresh install, one remembered switch; on for your machine.
2. Which panes reach your own phone automatically? — rec: every pane with a screen, guest agents included.
3. PWA first, then a Capacitor shell over the same `app/` via TestFlight? — rec: yes.
4. Per-device connect tokens in Phase 1? — rec: yes.
5. WebAuthn: drop from the threat table? — rec: drop.
6. Let your own `full` devices admit knocks, decide guest prompts and grant control? — rec: yes.
7. A diff view on the phone in Phase 2? — rec: later.
8. Keep one origin (join.relay-terminal.ai) rather than adding app.relay-terminal.ai? — rec: keep one.

Answers here or in the terminal go into `## Decisions`; Phase 1.1, 1.2 and 1.4 need none of them
and can start on a word.
