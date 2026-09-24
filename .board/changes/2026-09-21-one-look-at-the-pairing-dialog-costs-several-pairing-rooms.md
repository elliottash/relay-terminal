---
id: PRM2
type: work
status: needs-verification
labels: [bug, remote]
component: [gui, remote]
assignee: agent
implemented_by: kimi/kimi-k3
session: 72728150-6f0a-4029-9965-3a4dc8f8022f
rank: m
created: '2026-09-21'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'the pairing dialog shows one QR and one code, no re-mint on repeated announcements, and a 429 message with New code instead of a dead code', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Claude Code session on #SWPH, 2026-09-21: found by the hosted drive `docs/qa_evidence/2026-09-21-swph-hosted-drive/`'
links: {plans: [], commits: [a3594364, cf823284, 2c947f72], evidence: [docs/qa_evidence/2026-09-21-swph-hosted-drive/notes.txt, docs/qa_evidence/2026-09-22-remote-delivery/, docs/qa_evidence/2026-09-24-tryit-PRM2/], related: [FR1C, SWPH], github: null}
---
# One look at the pairing dialog costs two to four pairing rooms, and a code shown across an off/on is dead

## Issue

Two faults in #FR1C's pairing dialog, measured by the #SWPH hosted drive on 2026-09-21:

- The hosted rendezvous allows 20 pairing rooms an hour per desktop. Opening "Pair a phone…" once
  costs 2–4 of them (the QR's room, the typed code's room, and re-mints as the dialog settles); the
  drive used 17 in one run and its second attempt was refused with `429 too many pairing rooms this
  hour`. An owner pairing an iPhone and an iPad, reopening the dialog a few times, can lock himself
  out for an hour. Fix shape: one room per dialog opening until it is used or expires (the QR and
  the code can share nothing, by #FR1C's design, so that is two), no re-mint on refresh, and a
  sentence in the dialog when the rendezvous answers 429 instead of a dead code.
- With the dialog open, switching remote control off and on from the plug menu leaves the dialog
  showing a code that no longer works; reopening mints a good one. The dialog should mint again, or
  say the code ended, when `remote_state` goes off and on under it.

## Done means
Opening the pairing dialog spends exactly two rendezvous pairing rooms (one for the QR, one for the typed code), and repeated `started`/`remote_state` announcements spend none — failure shows as the rendezvous's `429 too many pairing rooms this hour` on an early reopen. With the dialog open, switching remote control off clears the QR and the code and says they have ended; switching it on mints credentials that actually pair — failure shows as a code on screen the phone rejects. When the rendezvous refuses with 429, the dialog shows the refusal and a New code button rather than a dead code.

## Execution Summary
Pairing waits for the configured service destination to be online, then requests one QR and one typed-code room. Repeated started/remote_state announcements reuse those credentials. Stop clears both displays; restart requests fresh ones. A failed request leaves the error visible and offers New code. Xvfb driver exercises the real dialog and its outgoing sidecar requests.

## Tests
- `ctest --test-dir build -R '^(sharing|remotesettings)$' --output-on-failure` — 2/2 pass, 2026-09-24
- `RELAY_KEYRING=off python3 -m unittest tests.test_remote_gui_host` — 51 tests OK, 2026-09-24
- `manual: docs/qa_evidence/2026-09-22-remote-delivery/` — run.py drives the actual dialog under Xvfb; PASS 2026-09-24, pairing.png regenerated

## Plan
**Goal.** One look at the pairing dialog costs exactly two rendezvous pairing rooms (QR + typed code), an off/on of remote control never leaves a dead code on screen, and a 429 from the rendezvous is visible in the dialog with a way forward.

**Findings.** A codex session on 2026-09-22 reports implementing this and left evidence in `docs/qa_evidence/2026-09-22-remote-delivery/` (README says "Checked from main at 82acbc04993a plus this session's changes"), but the card is still in Executing, so first establish what actually landed:

- `src/RemoteShare.cpp` — `RemoteShareDialog::refreshPairingService()` (~line 1468): waits for the configured rendezvous to be online before spending rooms, returns early when `m_pairingBase == share.base()` (no re-mint on repeated `started`/`remote_state`), clears QR + code and shows "Remote control is off. These pairing codes have ended." when sharing stops, re-mints on restart. `askPairCode()` (~1507) revokes the live code before minting another. The `failed` handler (~1434) shows "No code was made: …" and re-shows the New code button — the README claims a 429 stays visible this way.
- Sidecar side: `remote/gui_host.py` `pair_code` (~1257) and `host.open_pairing()` in `remote/host.py` (~955); the rendezvous enforces `MAX_ROOMS_PER_HOUR = 20` → 429 in `rendezvous/server.py` (~line 218/402).

**Steps.**
1. `git log --oneline -15` and `git status`; check whether the 2026-09-22 changes are on main or still uncommitted in the tree (another session may hold the files). If the behaviour above is already on main, skip to step 4.
2. If missing, implement in `src/RemoteShare.cpp` only: the destination-online wait, the `m_pairingBase` re-mint guard, the off-clears/on-remints behaviour, and the failure state with New code. Keep the sidecar untouched — `gui_host.py` already mints one code per ask.
3. Build with `scripts/relay-build` (never bare `cmake --build`) and commit with `python3 scripts/land.py` per CLAUDE.md — this is a shared checkout on main.
4. Verify: `ctest --test-dir build -R '^(sharing|remotesettings)$' --output-on-failure`, `RELAY_KEYRING=off python3 -m unittest tests.test_remote_gui_host`, and `python3 docs/qa_evidence/2026-09-22-remote-delivery/run.py` (drives the real dialog under Xvfb: settled startup sends one `pair` + one `pair_code`, repeated announcements send none, stop/restart, 429 stays visible with New code). Refresh the evidence README if anything changed.
5. Land the card in needs-verification with the evidence path. No `## QA checklist` — a separate session verifies.

**Risks.** `src/RemoteShare.cpp` is shared; land.py's contested-hunk review may require `--confirm`. The Xvfb driver compiles `driver.cpp` against build objects — a stale `build/` gives a false PASS, so rebuild first. The iPhone/iPad and lockout-hour behaviour against the hosted rendezvous stays human QA; do not claim it.

**Verify.** The three commands in step 4, all PASS; evidence at `docs/qa_evidence/2026-09-22-remote-delivery/`.

## Try it
Open: `docs/qa_evidence/2026-09-24-tryit-PRM2/01-pairing-ok.png`, then `02-after-429.png` — the real pairing dialog rendered from current `build/` objects, first with live credentials, then after the rendezvous refused with 429. Staged by `docs/qa_evidence/2026-09-24-tryit-PRM2/stage.sh` (no network, rerunnable; fixture in `/tmp/claude-1000/tryit`).

Task: read the second image as if you had just hit the hourly pairing-room limit yourself.

Question: could you tell what happened and what you would do next? (~2 min)

Expected is sealed in `docs/qa_evidence/2026-09-24-tryit-PRM2/expected.md`; staging limits in `staging-notes.md` (captures are asserted by the driver, not clicked live; real-rendezvous lockout stays human QA).
