<!-- relay:entry 20260921T022500Z-a1 author=claude-code kind=plan -->
### Claude Code · 2026-09-21 02:25
Filed from the owner's words: "the switchboard is the best thing we have to let a phone drive the
system. so build that now". Measured first: the Switchboard is unreachable from a phone (no
terminal view to share, no board view in `app/`, no `board_*` request accepted from a device, and
the Switchboard's own `BoardWorker` is invisible to the hub). Plan and the three-way contract are
in the card body. Claimed, executing: three subagents in parallel — the desktop bridge, the hub,
the phone view — then the drive, a deploy, and pairing the owner's iPhone and iPad with him.

<!-- relay:entry 20260921T111500Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 11:15
Tasks 1–3 landed and the web app is deployed from `39c94b91`. Desktop bridge (`38659350`,
`2c466481`): `src/BoardRemote.{h,cpp}` between the tab's `BoardWorker` and `RemoteShare`; starts
the worker on demand, rebuilds every request field by field, runs Execute and Verify through the
desktop's own hooks, strips paths, says every remote write in the status line. Hub (`5eb5699e`):
ten allow-listed requests at `full` only, `remote/board_state.py` refuses paths and over-long text,
rate limits, audit without text, the `card_waiting` push, protocol §17. Phone (`0abb4df0`,
`fd9d2caf`, `39c94b91`): the Switchboard row with the waiting count, stages with "Waiting on you"
first, the card page with Markdown built as DOM nodes, Answer / Comment / Discuss / Plan, Move,
Execute, Verify, Stop, New card, the offline outbox, iPad two-column. Deviations from the card's
contract were carried between the agents as they landed and are normative in §17. Known
follow-ups: a phone-started Discuss or Plan shows no busy strip on the desktop's card page
(`src/BoardPane.cpp`, held by other sessions); two windows on two boards share the hub's card
memory (one spurious `card_waiting` possible). The hosted drive is running.

<!-- relay:entry 20260921T114500Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 11:45
Task 4: the hosted drive passed all thirteen Switchboard steps and #FR1C's 14–19 against
join.relay-terminal.ai on a clean export of main (`docs/qa_evidence/2026-09-21-swph-hosted-drive/`,
39 notes, 0 failures). It found and fixed a desktop crash (`7fce9ef8`), Stop leaving the busy mark
(`7b5e00c8`), a missing audit line (`271849fe`) and the stale web theme (`0fda2acb`); deployed from
`b2632bf5`. #PRM2 and #SDR1 filed. The owner was emailed the link and the pairing steps at his
request. To needs-verification; the checklist is his iPhone and iPad.

<!-- relay:entry 20260921T114501Z-c2 author=claude-code kind=question -->
### Claude Code · 2026-09-21 11:45
1. Answering a question from the phone (or the desktop) writes your `decision` but leaves
   `waiting_on: owner`, so the row keeps saying "1 waiting on you". Should an owner's decision
   clear `waiting_on` by itself? Recommendation: yes, on any owner `decision` entry, and the agent
   sets it again if it still needs you; that is what makes the count trustworthy from a phone.

<!-- relay:entry 20260922T012002Z-d1 author=claude-code kind=note -->
### Claude Code · 2026-09-22 01:20
**Handoff.** All three build tasks plus the hosted drive are landed and deployed; the owner opened
Relay himself on 2026-09-21 rather than this session doing it, so task 4 is marked done on that
basis. What is open is the QA checklist above, on the owner's paired iPhone/iPad — no more code is
needed for it to pass. Known follow-ups that are NOT blocking, filed as separate cards or noted in
Execution Summary: no busy strip on the desktop's card page for a phone-started Discuss/Plan
(`src/BoardPane.cpp`, needs whoever holds it), a Verify pane headed "Execute" (`src/Pane.h`), one
possible spurious `card_waiting` with two boards open at once (needs a `board_key` from the
bridge), #PRM2 and #SDR1 (filed, unclaimed).
