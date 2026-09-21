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
