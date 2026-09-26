---
id: MJG6
type: work
status: planned
rank: zzzzzzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# relay-to-relay messages seems not to be working

## Issue
the relay inter-agentmessages dont seem not to be working

check out session 948260afe06a47a29e41a1a6bd01b15a

the agent tried to message the other.

excerpts:

✦ did the sent message work


Straight answer: half of it.

The socket message worked mechanically but missed. relay-terminal-2c received it and replied twice— so delivery is fine. But it wasn't #BRD3; it forwarded
to relay-terminal-59, which also wasn't. The #BRD3 agent runs inside a Relay pane, not as a ClaudeCode session, so it has no socket address at all. No
amount of SendMessage reaches it.

One useful thing came of it anyway: relay-terminal-2c warned me the shared tree wouldn't compile (CurrentTextComboBox.h extracted from Pane.h), then
corrected itself minutes later when #BRD3 landed the include. Neither affected my commits — land.py's gate builds the landing tree, not the shared one.

The thread note is written but not confirmed read. It's in issues/threads/BRD3.md on disk, which is what the board worker actually reads, so it will be seen
by whoever next opens that card. But there's no reply, and that agent may finish its turn without re-reading its own thread — so I would not call it
delivered.

## Planning notes

Freshness check, 2026-09-26: the original diagnosis is historical. #R5TC has landed the
`pane_list`/`pane_send` backend and GUI path and is in needs-verification with all 15 tasks
checked. `backend/relay_core/panes.py`, `src/PaneDirectory.cpp`, and `tests/test_panes.py` exist.
The source commits include `d33c1d6d` and `c4f87618`. This report now needs its original
scenario verified, not another implementation of #R5TC. Guest-to-Relay messaging is outside the
native-pane scenario here.

## Done means

An agent in one Relay pane sends to an agent working a card in another pane. The recipient gets
it when busy and when idle, can answer through `pane_send`, and the sender sees the answer. A
message merely written into a card thread without a live recipient response does not pass.

## Plan

**Goal.** Close the original report with a live regression check against #R5TC's implementation.

**Steps.**
1. Re-read the original session `948260afe06a47a29e41a1a6bd01b15a` only if its transcript
   is still available, to distinguish Claude Code socket addressing from Relay's new pane address.
2. In an isolated two-pane Relay run, send to a card-working recipient while it is busy and again
   while idle. Observe the delivery row and reply in both cases. Record the exact outcome and
   evidence on #R5TC for its verifier.
3. If both pass, close this report as done and link #R5TC's evidence. If either fails, record the
   precise address, delivery or reply failure here and keep #R5TC in verification.

**Risks.** A guest CLI has its own session addressing. Do not interpret a guest socket reply as a
Relay-native `pane_send` pass. Guest-to-Relay is a separate feature question if the owner wants it.

**Verify.** The targeted `tests/test_panes.py` battery and the two live busy/idle cases. No owner
choice of Execute card is left: #R5TC already owns and has landed the build.
## Tests

### Check: worker cross-pane battery (2026-09-26)
`python3 -m unittest tests.test_panes` — PASS, 17 tests in 0.053 s. Covers the worker's simulated busy delivery and idle wake outcome, framing, depth rule, refusal and cap. The GUI delivery in this battery is `FakePane`, so this is not a live two-pane result.


### Live two-pane probe (2026-09-26)
Ran the built `build/relay` under Xvfb with isolated XDG paths and a loopback scripted model. `python3 -m unittest tests.test_panes` also passed 17 tests (0.057 s). The synthetic recipient prompt was `CARD-WORK-BUSY`; this was an agent turn, but not a claimed Board card.

The sender in p1 called `pane_send` to p2 during the recipient's busy turn. Its tool result said `outcome: delivered` and `busy: true`. At p2's next step boundary, the Relay frame contained `BUSY-PING reply to p1`; p2 called `pane_send` with `BUSY-REPLY received`, and p1 displayed that reply. This confirms a live busy-path round trip.

After p2 displayed its completed-turn checkmark, the drive waited 20 seconds and sent `IDLE-PING` from p1. The tool result still said `outcome: delivered` and `busy: true`, so the idle wake path was not exercised. No `IDLE-REPLY` appeared in either pane. The earlier p2 reply itself had produced a `woke` result for idle p1. That establishes wake delivery in the reverse direction, but does not satisfy this card's idle recipient reply requirement. The drive used the previously built binary and did not build current source. Screenshots and model-request trace are in `/tmp/mjg6_live/` for this session.

## QA checklist

- [x] Rechecked publication mode: `.git/relay-publication.json` now reports `mode: queue`.
- [x] Read #MJG6 and #R5TC cards and threads; #R5TC's backend and pane-side commits are landed.
- [x] Ran the targeted worker battery (17 passing).
- [x] Observe live busy delivery and a return `pane_send` answer.
- [ ] Observe an idle p2 wake and return answer while p2 is working a claimed Board card.

## Verdict

Open. The live busy-path reply passed. The idle-path reply remains unverified because p2 was still reported busy on the second send, despite a completed-turn indicator after 20 seconds. The synthetic recipient was not a claimed Board card. Keep #MJG6 open and #R5TC in needs-verification until a true idle card-working recipient wakes and replies.
