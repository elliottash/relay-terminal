# Harness steering (#QG4C)

Backend implementation: `HarnessProvider` leases pending steers at tool start/result events;
request ledger and queue acknowledgement wait for native acceptance. Rejections restore the
lease, with no retry in that native turn. Late duplicate callbacks cannot consume another batch.
Uncertain writes/timeouts close the guest and fail the turn, leaving the queue paused for review.
Ordinary queued prompts retain the existing dispatcher behavior.

Codex protocol was verified locally with `codex app-server generate-json-schema --out
/tmp/qg4c-codex-schema`: `v2/TurnSteerParams.json` requires `threadId`, `expectedTurnId`, `input`.
Claude's installed executable contains its stream-json implementation: UUID user frames enter
`acceptUserFrame`, `priority` is passed into its message queue, and replay-user-messages emits
UUID-correlated `isReplay` user frames when committed/merged. The adapter enables that flag and
continues reading when unacknowledged input crosses a native result boundary.

Validation (fake harnesses/transports only; no live paid guest turn):

```
PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_steer tests.test_guest_harness_provider tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_queue
```

Result: **260 tests passed** (4.667 seconds). `tests_check` resolves every named test; its
pre-commit orphan warning reflects that the card has no linked implementation commit yet.
The board-wide format check reports no #QG4C findings (other cards have existing warnings).

New coverage: tool-start delivery; input arriving during a tool delivered at result; rejection;
unacknowledged lease restoration; duplicate and late duplicate acknowledgements; native Codex
active-turn precondition and timeout; Claude same-turn replay, result-boundary continuation and
failed write. Existing adapter/provider/queue regressions run alongside these.
Initial combined runs exposed existing races in `test_now_runs_while_queue_paused` and
`test_cancel_pauses_queue_until_resume`: cancellation after agent_started can happen before
the fake provider records its first prompt. Both fixtures now wait for the provider delta,
which proves the provider is running before cancellation. Production ordinary-queue behavior
was not changed.

Owner live verification:
1. Restart Relay's worker (opening a fresh pane in the checkout build is simplest), select Claude
   or Codex harness. Python modules are loaded at worker startup; no C++ rebuild is needed for
   this backend patch. The separate TUI patch requires its own build.
2. Ask for several tool calls with a slow command. While it runs, submit a unique steering
   instruction using “next tool call”. Verify it is acknowledged during the same Relay turn,
   appears in the guest response, and the row disappears once, without a duplicate follow-up.
3. Queue an ordinary prompt and verify it still waits for the active turn to finish.
4. Repeat for both guests; test a steer near the final tool/result boundary.

Limit: tool notifications are observations, not an execution barrier. An already-started tool
can finish before native steering is incorporated. Claude may complete a queued continuation
inside the same Relay turn if its input crosses a native result boundary. After an uncertain
transport failure, inspect what ran and reselect the guest before intentionally resuming input.
Live acceptance is intentionally left to the owner. This delegated guest had no relay_board
MCP tools in its discovered catalog, so board updates used POLICY.md's file fallback.


Parent review follow-up: Claude continuation results now retain earlier final text in both the
returned transcript and final done event. Leased steers remain visible through unrelated queue
updates, clear and cancel; remove/unsteer honestly refuse withdrawal once transport delivery is
in flight. Clear reports that pending native input cannot be withdrawn. Idle reset clears any
orphan lease; turn-end cleanup returns any lease a provider did not settle. Regression tests
exercise these lifecycle paths and accumulated result text. The focused rerun covers the steering,
Claude adapter and queue modules (128 tests).

Claude timing evidence is source-level plus fake-stream regression, not a live acceptance claim:
the installed CLI admits additional UUID user frames into its priority queue and replays committed
input; Relay writes priority `next` at observed tool start/result events. Native incorporation may
occur after the observed tool, and near completion it can become a continuation within the same
Relay turn. Owner live testing is still required to establish behavior on their CLI session.
