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

<!-- relay:entry 20260920T220900Z-b1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 22:09
Owner: "kick it off with efficient orchestration on opus subagents". Taken as go on the plan under
the eight recommendations (recorded in `## Decisions`); the card moves to executing. Wave 1: four
Opus subagents on disjoint files — desktop always-on (C++), sidecar always-on (Python), deploy and
icons, phone inbox/offline queue/notification switches. Wave 2 after they land: connect tokens,
and the pane view's Stop / Recap / question rows / admit-from-phone. Wave 3: the headless drive.

<!-- relay:entry 20260920T224500Z-c1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 22:45
Phase 1.4 landed (`80706293`, `38cf319b`): `rendezvous/deploy.sh` (export from a rev, rollback copy,
restart, health and per-file sha check, `--check`, `--rollback`, `--install-unit`), run twice
against elliott-main-1; join.relay-terminal.ai now serves exactly main (24 files match). PNG,
maskable and apple-touch icons from the one `app/icon.svg`; `tests/test_web_manifest.py` (12).
The previous hand deploy had stripped trailing newlines from every served file. The push icon line
in `app/sw.js` was routed to the phone-app agent, who owns that file.
