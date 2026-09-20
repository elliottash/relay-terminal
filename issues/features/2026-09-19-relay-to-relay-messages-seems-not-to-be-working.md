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

## Plan
**Goal.** Make this card's scenario real: the agent in one pane messages the agent in another
pane of the same Relay, and it is delivered and answered. Nothing is broken — the feature was
never built. #R5TC ("One pane's agent sends a message to another, inside one Relay") is the
designed fix and sits `planned`, unexecuted, so the agent in session `948260…` improvised over
Claude Code's own cross-session sockets, which can only ever reach other `claude` sessions, never
a Relay-native pane agent like the one on #BRD3. This card is the report; the build is R5TC's.

**Findings** (verified 2026-09-20).

- No cross-pane path exists in the worker: the agent-facing surface is `agent`, `agent_message`,
  `agent_wait` (`backend/relay_core/subagents.py:61`), addressing subagent threads of the *same*
  pane only; `backend/relay_core/tools.py` has no `pane_*` family; a worker is told nothing about
  any pane but its own (`validate_context`, `backend/relay_core/agent.py:204`, a closed allow-list).
- #R5TC is planned, not executed: no `backend/relay_core/panes.py`, no `src/PaneAddress.*` /
  `PaneDirectory.*` in `src/`. Its plan — `pane_list` / `pane_send`, notices delivery plus the
  idle wake, depth-one wakes, `p<n>` addressing, protocol §29, `tests/test_panes.py` — is the plan
  of record, with the owner's decisions already on that card. Do not re-plan it here.
- What the sender used instead was Claude Code's cross-session socket messaging (its `SendMessage`),
  whose directory only contains `claude` CLI sessions — `relay-terminal-2c`, `relay-terminal-59`,
  both named after this repo's directory. The #BRD3 agent is a Relay-native agent in a Relay pane,
  so it has no socket address there and cannot be reached that way. Delivery worked; addressing
  could not. (Confirm from the transcript — step 1.)
- The sender's fallback, appending to `issues/threads/BRD3.md`, is real but fire-and-forget:
  nothing notifies a running pane agent that its card's thread grew, so that agent may finish its
  turn without re-reading it — exactly the excerpt's "not confirmed read".
- #GT7X (claude/codex guests) is landed, but R5TC reserves `party: "guest"` and refuses it
  `not_supported` (Relay→guest hook), and a `claude` session can never call `pane_send`. So
  guest→Relay messaging is in no plan at all today.

**Steps.**

1. Confirm the diagnosis from the record: open session `948260afe06a47a29e41a1a6bd01b15a`
   (Sessions pane; files under `sessions_root()` / `<workspace-digest>/`,
   `backend/relay_core/sessions.py:46`) and pin down which mechanism the sender used and what
   answered (`relay-terminal-2c`, `relay-terminal-59`). Post it as evidence on this card. If it
   was not Claude Code sockets after all, stop and re-plan here before building.
2. Execute #R5TC's plan exactly as written on that card — its Tasks list end to end
   (PaneAddress/PaneDirectory, `panes.py`, delivery + wake, depth one, budget, framing, GUI rows,
   protocol §29, `tests/test_panes.py`). Commits and implementation evidence are recorded on
   #R5TC, not here. Per `WARP.md`: build through `scripts/relay-build`, land through
   `scripts/land.py`.
3. Prove *this* card's scenario, beyond R5TC's generic two-pane run: a live Xvfb run where the
   agent in one pane messages the agent in a second pane that is working a card (a #BRD3-shaped
   terminal-pane agent) — busy (delivered at a step boundary) and idle (woken, no approval) — and
   the reply returns to the sender. Evidence under `docs/qa_evidence/2026-MM-DD-relay-to-relay-messages/`.
4. Record the two adjacent gaps this card surfaced, without fixing them here: (a) no notification
   when a card's thread file grows — the sender's only working fallback channel; (b) the
   guest→Relay direction. File each as its own card only if the owner asks (see Risks).
5. Close: move this card to `done` pointing at #R5TC's commits and the step-3 evidence, with the
   step-1 diagnosis in the body.

**Risks.**

- **One decision for the owner — whose Execute carries the build.** Recommendation: #R5TC's; it
  holds the plan, tasks and acceptance, and this card keeps steps 1, 3–5 as its report-and-close.
  If you press Execute here instead, this card runs R5TC's task list and the commits go on both.
- Guest→Relay messaging (a `claude`/`codex` session reaching a Relay pane agent) is out of scope
  everywhere: R5TC is relay-native panes by owner decision, and its `party: "guest"` hook covers
  the other direction. If the complaint includes the claude side talking to Relay agents, say so
  and it becomes its own card. Recommendation: keep it out of this one.
- The step-1 transcript may show another improvised path (e.g. driving `claude -p` through
  `run_command`); the remedy is unchanged, but the evidence should say so before anything builds.
- `relay-terminal-2c` replying twice is Claude Code's own behaviour; nothing here fixes it.

**Verify.** #R5TC's `tests/test_panes.py` battery (every refusal code, per-turn cap, idle wake with
no approval, depth one, budget expiry, the exact inbound frame) plus both live Xvfb runs. This card
closes only on the excerpt's scenario passing: sender in one pane, recipient agent in another,
message delivered *and* answered, with the sender's row reading `woke it` or `delivered · it is
busy`.
