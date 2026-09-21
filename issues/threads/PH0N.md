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

<!-- relay:entry 20260920T231500Z-d1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 23:15
Desktop half of Phase 1.1/1.2 landed (`2c59a720`, `f39cb626`, `8fc8d60b`, evidence `baebd13a`):
Options › Remote (switch + address, `remote/alwaysOn`, `remote/address`), sidecar at launch, every
pane published on open and withdrawn on close, the plug menu's status line, dot and "Disconnect
all" (turns the switch off; the sidecar has no drop-devices message). `start` now carries
`always` and `address`; `remote_state` is parsed. #WMXN fixed on the way (`reason: "manual"`).
Found and fixed: a SIGSEGV at quit whenever a pane was shared (`~RelayWindow` now disconnects
from `RemoteShare`). Sidecar half still running.

<!-- relay:entry 20260920T234500Z-e1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 23:45
Sidecar half landed (`8e1e740f`): `start.always` brings the service up at the chosen address and
keeps it there (register again before every retry, 1 s → 60 s jittered backoff, never moves on its
own); `remote_state` on every change; owner devices counted, guests never; auto-published panes
still cut to the invite for guests (tested). Found on the way: a restarted rendezvous forgot the
hub's token and the old loop re-dialled with it forever (4401). Phase 1.1 and 1.2 are complete.
Running: connect tokens (1.3), the pane view's Stop / Recap / question rows / admit (2.6), the
inbox / offline queue / notification switches (2.5, 2.7, 2.8).

<!-- relay:entry 20260921T001000Z-f1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 00:10
Phases 2.5, 2.7, 2.8 landed (`e97c5fc8`): inbox status chips with "needs you" first and the app
badge; a prompt or Stop sent while the socket is down is kept and sent once on resume (real-drop
test); reconnect on `pageshow` and `online`; notifications as two switches ("When an agent finishes
or fails" = agent_finished, failed, plan; "When something needs me" = waiting_input, password);
tapping a notification opens that pane; the push carries Relay's icon; on iOS outside the
installed app the settings say to add to the Home Screen first. Found and fixed: Stop vanished the
moment the socket dropped. Missing wire field `updated` on the GUI's `pane` message is being added
by the orchestrator (`src/RemoteShare.cpp`) so "running · 3m" is exact.

<!-- relay:entry 20260921T010000Z-g1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 01:00
Phase 1.3 landed (`ee12ac1a`): a connect token per device, minted by the desktop (HMAC under a
secret derived from its identity key, length-prefixed fields), handed over only inside the Noise
session, presented as `ct` on `/v1/connect`; the rendezvous checks it statelessly, counts channels
per token (3), and answers a missing, bad or revoked token exactly like an unknown desktop.
Revoking a device posts `/v1/revoke` and closes a live channel. No shim: devices paired before
today pair again. Redeployed to join.relay-terminal.ai from `ee12ac1a` (26 files match main).
The Opus session limit cut the first two wave-2 agents off mid-work; Fable subagents took over
under the same land.py names. Phase 2.6 (pane view) still running.

<!-- relay:entry 20260921T013000Z-h1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 01:30
Phase 2.6 landed (`8e9b2e05`): Stop in the strip while a turn runs; Recap in the pane menu; the
worker's question drawn above the prompt box with a button per choice and Skip, a typed line
answering it agent-bound; `owner_asks` to `full` devices with Admit/Refuse, Run/Refuse, Allow/Deny,
raced against the desktop's Sharing pane so whichever answers first wins and the other row goes
(`request_gone`). Redeployed to join.relay-terminal.ai from `8e9b2e05`. Phase 1 and Phase 2 are
complete; next is the headless drive against the hosted address, then the owner's iPhone.

<!-- relay:entry 20260921T034500Z-j1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 03:45
Wave 3 landed (`c6c2f72e`, `4c0348a8`, `4cffa9b2`, `5cf0db2b`): the whole always-on path driven
live against join.relay-terminal.ai — a real Relay under Xvfb, headless Chrome as the phone, a
second browser as a guest — 23 of 24 steps PASS in `docs/qa_evidence/2026-09-21-ph0n-hosted-drive/`
(`drive.sh` reruns it). The one FAIL (a `/pair` reload on a paired device) is fixed and served
since the `5cf0db2b` deploy. Five product bugs found and fixed on the way; two filed (#WEVT,
#ADTR). Phases 1 and 2 are complete; the card moves to needs-verification with the iPhone
checklist as the acceptance. Owner decision left open: a recap on a desktop with no keys goes to
Relay Free through the summaries role (seen with `RELAY_KEYRING=off`); fine or not?
