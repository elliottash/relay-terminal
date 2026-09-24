---
id: 97EG
type: work
status: needs-verification
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: kimi/kimi-k3
session: 5ccf8d2d-fdab-4934-bf35-8c25491b3786
rank: 6e
created: '2026-09-18'
acceptance: a friend types a 4-letter meeting code and a 4-digit PIN on the join page and reaches the owner's knock row; the server never learns the PIN and cannot join or sit in the middle without guessing it online; three wrong PINs burn the code
source: 'owner, 2026-09-18: "is there a way to have like a sync code share, where i tell my friend a code and they type it on relay-terminal.ai to join me" … "i like option 2 from above -- how about 4 letter meeting code, 4 number pin code" … "go and build it, write a card and use subagents"'
links: {commits: [616a47e6, 37563ce4], evidence: [docs/qa_evidence/2026-09-18-join-with-meeting-code-and-pin/], github: null, plans: [], related: [W5N2]}
---
# Join a shared pane with a meeting code and a PIN

## What the owner asked for

Tell a friend two short things — **`BQRT` and `4829`** — and they type them on the join page to
reach you, instead of being sent a 141-character link. Zoom's "meeting ID + passcode", with the
property the rest of `#W5N2` is built on kept intact: relay-terminal.ai routes bytes it cannot read.

## Design

The two halves do different jobs, magic-wormhole style.

- **Meeting code** — 4 letters from `ABCDEFGHJKMNPQRSTUVWXYZ` (23; no I, L or O), about 280,000
  values. **Public**: the rendezvous uses it only to find a room. Allocated by the rendezvous,
  unique among live codes, case-insensitive on input.
- **PIN** — 4 digits, drawn on the desktop with a CSPRNG. **Secret**: never sent to the server.
  Both sides run **CPace** with it, so every guess is an online attempt against the desktop.
- **Three failed attempts burn the code** (and its invite). The **desktop** counts, never the
  server — the server is the party this design declines to trust. Chance a stranger (or the
  server) gets in by guessing: 3 in 10,000 per code, and a correct guess still only earns a knock
  that the owner admits by hand.
- **Lifetime** 10 minutes, one use. Success burns the code too.
- The code phase ends by delivering, sealed under the CPace key, **an ordinary invite fragment**
  (`v=1&d=…&i=…&r=…`). From there the existing path runs unchanged: Noise IK against `d`, `knock`,
  the owner's knock row, roles, control handoff, prompt approval (`docs/REMOTE-PROTOCOL.md` §10).
- Because CPace already rules out a man in the middle, the five-digit knock code is no longer
  the only defence against one; it stays on screen on both sides as an independent second check.

Known cost: anyone who knows the meeting code can burn it with three wrong PINs. The owner makes a
new one; nothing is exposed.

## Conditions from the #W5N2 owner (2026-09-18, binding)

1. The delivered invite is minted `uses = 1`, expiry ≤ 600 s, and **its first knock claims it** —
   a forwarded fragment cannot knock afterwards. It burns with its code, or if its knock is refused.
2. The rendezvous answers an unknown code exactly like an expired one; limits lookups per peer on
   top of `MAX_CHANNELS_PER_PEER`; keeps the code → room map only for the code's ttl, never in the
   7-day metadata retention.
3. A code room never reaches the Noise `Channel` path or a hub handler; a socket that has not
   finished CPace in 30 s is closed and counted; a code room takes a rendezvous slot like any client.
4. Audit, before each action: `code_create`, `code_attempt` (count, no PIN), `code_used`,
   `code_burned`, `code_expired`.
5. The five-digit knock code stays on screen on both sides, and the compare step stays.
6. Security tests in `tests/test_remote_security.py`: the rendezvous cannot produce the invite from
   what it relayed; three failures burn and a fourth is refused; a used code is refused; a
   forwarded fragment after success is refused; the draft's CPace vectors.
7. "Make a code" lives in the share window's invite row, in its own widgets.

## Wire spec (every part builds to this)

**CPace.** `CPACE-X25519-SHA512` exactly as in draft-irtf-cfrg-cpace (initiator-responder mode),
checked against the draft's published test vectors in both languages.

| Input | Value |
|---|---|
| `PRS` | the PIN, ASCII (`b"4829"`) |
| `CI` | `b"relay/meet/v1"` |
| `sid` | the **code room** id, UTF-8 |
| `ADa` (initiator = the guest's browser) | `b"guest"` |
| `ADb` (responder = the desktop) | `b"desktop"` |

Output: `ISK` (64 bytes). Derived from it with HMAC-SHA256 (key = `ISK`):

| Name | Message |
|---|---|
| `tag_b` (desktop proves it knows the PIN) | `b"relay/meet/v1 desktop" ‖ Ya ‖ Yb` |
| `tag_a` (guest proves it) | `b"relay/meet/v1 guest" ‖ Ya ‖ Yb` |
| `seal_key` (32 bytes) | `b"relay/meet/v1 seal"` |

Tags are compared in constant time.

**Rendezvous routes** (`rendezvous/server.py`; the same code runs at relay-terminal.ai and inside
the desktop's own sidecar):

| Route | Who | Body → reply |
|---|---|---|
| `POST /v1/codes` | desktop (authenticated like `/v1/rooms`) | `{desktop_id, token, room, ttl}` → `{code, expires_in}`; ttl capped at 600 |
| `GET /v1/codes/<CODE>` | anyone | → `{room}` or 404. Rate-limited per client address (10/min) |
| `POST /v1/codes/<CODE>/burn` | desktop | `{desktop_id, token}` → `{}`; the code stops resolving at once |

**The code room.** A room of its own, opened by the desktop with `/v1/rooms` (ttl 600), separate
from the invite's room. The desktop remembers which rooms are code rooms; a client that connects to
one is handled by the code handler, not by the Noise channel. Frames are UTF-8 JSON; binary fields
are unpadded base64url.

| # | Direction | Frame |
|---|---|---|
| 1 | guest → desktop | `{"t":"meet_a","y":Ya}` |
| 2 | desktop → guest | `{"t":"meet_b","y":Yb,"tag":tag_b}` |
| 3 | guest → desktop | `{"t":"meet_confirm","tag":tag_a}` — the guest sends this only after `tag_b` checked out |
| 4 | desktop → guest | `{"t":"meet_invite","nonce":n,"sealed":c}` — AES-256-GCM under `seal_key`, 12-byte random nonce, AAD = the meeting code (ASCII, upper case), plaintext = the invite fragment |
| – | desktop → guest | `{"t":"meet_error","error":"wrong_pin"\|"burned"\|"expired"}`, then close |

An attempt that ends any way other than a valid `meet_confirm` within 30 s counts as a failure:
bad tag, invalid point, closed early, timed out. The third failure burns the code (route above)
and its invite, and the desktop tells the GUI.

**Desktop sidecar** (`remote/gui_host.py`, owner-only like every §10.5 message):
`{"t":"code_create","pane":"p1","role":"editor"}` → `{"t":"code","code":"BQRT","pin":"4829","expires":600,"invite":"<id>"}`;
later `{"t":"code_state","code":"BQRT","state":"used"|"burned"|"expired","failures":N}`.
QA hook: `RELAY_REMOTE_CODE_FILE` names a file the GUI writes `BQRT 4829` to, beside
`RELAY_REMOTE_INVITE_FILE` — the PIN is a secret, so it is never logged.

## Tasks

- [x] CPace in Python and in the browser, both against the draft's test vectors (`remote/cpace.py`, `app/cpace.js`) <!-- t:d2 -->
- [x] Rendezvous routes, the code room handler and the sidecar message (`rendezvous/server.py`, `remote/meetcode.py`, `remote/host.py`, `remote/gui_host.py`) <!-- t:xx -->
- [x] Join page: meeting code and PIN form, the code phase, then the existing knock (`app/guest.js`) <!-- t:db -->
- [x] Share window: "Make a code", the code and PIN shown large, countdown, used/burned states (`src/RemoteShare.cpp`) <!-- t:z9 -->
- [x] Protocol doc (§10.7, with a sentence in §10.2 and the audit kinds in §10.6): a §10 subsection for this <!-- t:m2 -->
- [x] Live run: laptop (sphinxpad) joins a desktop pane by code and PIN in Chrome <!-- t:ge -->

## Execution Summary
Live run, 2026-09-20 (this Execute turn; the five build tasks were already in the tree from 2026-09-19). A real Relay desktop (clean worktree build of HEAD `ee12ac1a`) in an Xvfb jail made a code from its share window; a real Chrome on the same display joined through the desktop sidecar's own join page: meeting-code form → CPace in the page → sealed invite → knock → the owner's Sharing pane → "Admit as viewer" → the guest watching the pane. Four consecutive end-to-end passes (codes BKCG, SZQF, PRUV, UNXM). Evidence: `docs/qa_evidence/2026-09-18-join-with-meeting-code-and-pin/` — harness (`live.sh`, `drive_guest.mjs`), six screenshots, console log, README. Verified in the run: the five-digit codes matched on both screens (read off both and compared by the script); audit lines `code_create`/`code_used`/`knock`/`admitted` with `uses_left: 0`; the code struck through as Used; the PIN absent from the desktop's stores, rendezvous db and logs (only in the RELAY_REMOTE_CODE_FILE QA hook). Caveat: both ends ran on one machine (loopback) — this session cannot reach sphinxpad; the laptop redo is on the QA checklist. Note: between 18:44 and `4109f930`, main crashed at startup on fresh profiles (#561P, not this card); the run used the fixed HEAD.

## Tests
Automated, this turn: `python3 -m unittest tests.test_cpace tests.test_remote_meetcode tests.test_web_meet_code` — 51 tests OK (CPace draft vectors in both languages, the code-room handler, the browser peer). `python3 -m unittest tests.test_remote_security tests.test_remote_noise` — 63 tests OK (the condition-6 cases: the rendezvous cannot produce the invite from what it relayed, three failures burn and a fourth is refused, a used code is refused, a forwarded fragment after success is refused, the code room never reaches Noise). The 2026-09-19 `backend-and-bash` failures recorded on the card are gone from these modules; the suite's remaining 7–9 failures are in modules other sessions edit uncommitted (board_protocol, guest_hook, system_prompt, web_theme, remote_wire) plus load-flaky browser tests. Live: `docs/qa_evidence/2026-09-18-join-with-meeting-code-and-pin/live.sh` — self-checking, exits non-zero unless the code match, the audit lines and the PIN-absence all hold; passed four times.

## QA checklist
- [ ] Redo the live join the way the task words it: laptop (sphinxpad) → this desktop in Chrome, over the LAN or tailnet address, not loopback. The implementer ran both ends on one machine (Xvfb jail); the harness is `docs/qa_evidence/2026-09-18-join-with-meeting-code-and-pin/live.sh`.
- [ ] On the desktop: share window → "Make a code" shows the 4-letter code and 4-digit PIN large, with the 10-minute countdown; after the join it strikes through as Used.
- [ ] On the guest: the join page's "Have a meeting code instead?" form takes the code and PIN; a wrong PIN says so and the code field keeps focus; three wrong PINs burn the code (the desktop's share window says Closed; a fourth attempt gets the same answer as an unknown code).
- [ ] The knock row shows the same five digits as the guest's waiting screen; Refuse burns the invite (a second knock on the same fragment is refused).
- [ ] The code dies on its own in 10 minutes; the share window says Expired.
- [ ] The PIN never appears in logs, the audit log, or the rendezvous db (audit kinds: code_create, code_attempt with a count and no PIN, code_used, code_burned, code_expired).
- [ ] The rendezvous answers an unknown code exactly like an expired one, and a code room that has not finished CPace in 30 s is closed.
- [ ] Docs: docs/REMOTE-PROTOCOL.md §10.7 matches the behaviour above.
