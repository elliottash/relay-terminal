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

<!-- relay:entry 20260921T013000Z-j1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21
The drive's one red step is green. `rendezvous/deploy.sh` put `5d8ce281` on join.relay-terminal.ai
(the served and repo `app.js` now hash the same) and the whole thirteen-step drive was run again
against it: thirty checks, no failures, including a paired phone reloading `/pair` and the
"paste the pairing link" path an installed iOS app needs. Evidence refreshed in `d56c80aa`.
Two cards filed during the work carried ids outside the board's alphabet and are now #WEVT and
#ADTR (`894385bf`); #RCPF asks the owner whether a keyless desktop should spend Relay Free on an
automatic recap. Nothing is left on this card but Phase 3, on the owner's iPhone.

<!-- relay:entry 20260921T034500Z-j2 author=owner kind=comment mode=discuss -->
is this done

<!-- relay:entry 20260921T034500Z-j3 author=agent kind=comment mode=discuss model=kimi-k3 turn=b01394da11cb49fea974b728ea889ed4/f3cc2efd34644a0184f92d7bdafc89b9 -->
**Not quite** — Phases 1 and 2 are built, tested and deployed, but the card's acceptance is Phase 3, which only you can do.

**Done:** all 8 build tasks. Always-on sidecar, connect tokens, join.relay-terminal.ai serving main, inbox chips, Stop/Recap/question rows, offline queue, notification switches — plus a 23/24 headless drive against the hosted address (the one failure since fixed and redeployed).

**Left:** task t:c9 — install the PWA from join.relay-terminal.ai on your iPhone's Home Screen, pair, and use it over LTE for a working day (push on the lock screen, prompt from the bus, a Stop, the #KBFT keyboard checks). Evidence goes to `docs/qa_evidence/2026-09-2x-iphone-all-day/`.

Also open, from the last evidence entry: **a recap on a keyless desktop now goes through Relay Free's summaries role — is that fine with you?** Say the word and I'll record either answer in `## Decisions`.

<!-- relay:entry 20260921T034500Z-j4 author=owner kind=comment mode=discuss -->
could you explain that

<!-- relay:entry 20260921T034500Z-j5 author=agent kind=comment mode=discuss model=kimi-k3 turn=b01394da11cb49fea974b728ea889ed4/160db352066a448ea9275edc1ac70c63 -->
Sure — it's about the **Recap** button, and what powers it when your desktop has no API keys.

**The situation:** Recap ("what did this agent do?") needs a model to summarize the pane. Normally that uses whatever key the pane already has. But on a desktop run with no keys configured (the QA runs used `RELAY_KEYRING=off`), there is nothing to summarize with.

**What was built:** in that case, Recap falls back to **Relay Free** — the included, quota-limited hosted provider — through its *summaries* role. So a keyless desktop can still produce a recap, but it spends from the Relay Free quota to do it.

**Why it's a decision and not just a default:** it's the first place a free, automatic, user-triggered feature silently consumes hosted quota, and it means pane content leaves the machine to Relay's hosted summarizer — which matters on a feature whose whole point is end-to-end encryption. The rendezvous and push relay never see content; this path would.

**The question on the table (card #RCPF):** is that trade fine — Recap on a keyless desktop quietly uses Relay Free — or should Recap just say "no model configured" instead? On your own machine, which has keys, you'd never hit this path either way.

Say "fine" or "not fine" and I'll record it in `## Decisions`.

<!-- relay:entry 20260921T034500Z-j6 author=owner kind=comment mode=discuss -->
oh yeah, thats fine.

<!-- relay:entry 20260921T034500Z-j7 author=agent kind=event model=kimi-k3 pane=switchboard turn=b01394da11cb49fea974b728ea889ed4/c4f4c58d805647b4887292ca2172c1b8 -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260921T034500Z-j8 author=agent kind=decision model=kimi-k3 pane=switchboard turn=b01394da11cb49fea974b728ea889ed4/c4f4c58d805647b4887292ca2172c1b8 -->
Owner: "oh yeah, thats fine." — a Recap on a keyless desktop falling back to Relay Free's summaries role is approved (answers the open question carried from wave 3; #RCPF). Recorded in `## Decisions`.

<!-- relay:entry 20260921T034500Z-j9 author=agent kind=comment mode=discuss model=kimi-k3 turn=b01394da11cb49fea974b728ea889ed4/c4f4c58d805647b4887292ca2172c1b8 -->
**Recorded.** Your "oh yeah, thats fine." is now a decision on #PH0N's `## Decisions`, and I noted it on #RCPF too, so that card's open question is answered.

That leaves the card with exactly one thing: **your iPhone day** (task t:c9). Whenever you're ready — install from join.relay-terminal.ai to the Home Screen, pair, and take it out on LTE.

<!-- relay:entry 20260921T034500Z-ja author=owner kind=comment mode=discuss -->
ok im connected, but i coudlnt do anything

<!-- relay:entry 20260921T034500Z-jb author=agent kind=note model=kimi-k3 pane=switchboard turn=56a4a9ed565b4b3cac5307c3949f2568/42d7c2f3225a4579b98a927d6e3c99ec -->
Phase 3 began 2026-09-21: the owner paired the iPhone and connected, but "couldn't do anything". Most likely cause from the code: the desktop pairing dialog (`RemoteShareDialog::showAsk`, src/RemoteShare.cpp) has three buttons — Refuse (default, focused), "Allow viewing" (grants `view`), "Allow typing" (grants `full`) — and a `view` device gets no prompt box, no Stop, no Recap in the app (`app/app.js:805`: compose needs `agent` or `full`). Advised: revoke the phone in the share dialog and re-pair, choosing "Allow typing". Side finding, filed as a bug: there is no way to raise a paired device's grant short of revoke-and-re-pair (`set_capability` exists in remote/identity.py:306 but nothing in the GUI sends it).

<!-- relay:entry 20260921T034500Z-jc author=agent kind=comment mode=discuss model=kimi-k3 turn=56a4a9ed565b4b3cac5307c3949f2568/42d7c2f3225a4579b98a927d6e3c99ec -->
The pairing dialog explains this. Let me check whether a card already covers it, then record it.









**Most likely: you paired it as view-only.** When the phone knocked, the desktop dialog had three buttons — **Refuse** (default), **Allow viewing**, **Allow typing**. "Allow viewing" grants `view`, and a view-only phone gets exactly what you saw: panes and screens, but no prompt box, no Stop, no Recap — read-only.

**Fix (one minute, at the desktop):**

1. Open the share dialog (the plug menu) — the devices list at the bottom should show your iPhone as `view`.
2. Select it, **Revoke selected**.
3. Pair again (scan the QR), and this time click **Allow typing**.

Then the phone gets the composer, Stop, Recap, question rows — the whole Phase 2 build.

If the devices list already says `full`, tell me what you *do* see on the phone (pane list? screens? a "read only until you take over" line?) — that would make it a different bug.

Card writes: noted the Phase-3 start and diagnosis on **#PH0N**, and filed **#RSJY** — there's no way to raise a device's grant short of revoke-and-re-pair, and the phone never says *why* it's read-only. Worth fixing before your next trip.

<!-- relay:entry 20260922T012000Z-k1 author=claude-code kind=note -->
### Claude Code · 2026-09-22 01:20
**Handoff, for whoever picks this up next.** All of Phases 1 and 2 are landed, deployed and driven
live against the real join.relay-terminal.ai (evidence linked above). What is left is entirely
Phase 3: the owner's own iPhone and iPad, over LTE, for a working day. Nothing here needs more
code before that — the QA checklist just above is the whole of it, and it now points at the
current pairing flow (#FR1C's "Pair a phone…" in the plug menu, not the old Options path). While
you are at it, #SWPH (the Switchboard on the phone) and #SHRP (the redesigned Sharing pane) use
the same pairing and the same devices, so their checklists can run in the same sitting. Four small
bugs the drives found are filed but unclaimed: #WCLS, #AUDL, #PRM2, #SDR1 — none blocks pairing or
day-to-day use. Phase 4 (a Capacitor shell) is a decision for after the week, not before it.
