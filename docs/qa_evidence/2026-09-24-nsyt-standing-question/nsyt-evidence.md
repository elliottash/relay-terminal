# Evidence — #NSYT: the standing init question answers the agent's parked card call

Collected 2026-09-24 (UTC) from this machine while landing the fix.

## The incident (before the fix)

`~/.local/share/relay/logs/relay.log`, 2026-09-24:

```
16505: 2026-09-24T23:13:52.902Z INFO  relay.gui project init asked pane=56ed3b35 trigger=agent-work project=/home/elliott/repos/modalities
16842: 2026-09-24T23:15:57.674Z DEBUG relay.gui switchboard event=board_init_request pane=56ed3b35 kind=board_init_request reason=agent-card
16843: 2026-09-24T23:15:57.687Z DEBUG relay.gui worker event=tool_result pane=56ed3b35 tool=board_create_card …
17195: 2026-09-24T23:18:02.708Z ERROR relay.gui switchboard … "This project has no Board yet. Create one first (board_init), then import or sync into it."
17212: 2026-09-24T23:18:11.411Z DEBUG relay.gui switchboard event=board_init_request pane=8dd7735d reason=card-command
```

- Request answered **13 ms** after it was raised — no dialog was shown for it; the pane
  auto-answered `accept: false` because its own agent-work question (23:13:52) was still
  on screen, unanswered.
- `~/.local/share/relay/state/projects.json`: `"declined": []` — the user never clicked No.
- Pane scrollback `~/.local/share/relay/sessions/83e6ce670835f99d/c6f7263dd6a14a30b266bceb6f674719.scrollback.txt`
  lines 66–73: the agent's tool result "This project has no Board and the user declined to
  create one. Do not call the board tools again in this conversation"; no "Not now" inline line
  anywhere in the scrollback.

## The fix

- `src/Pane.h` `handleBoardInitRequest`: when `decide(AgentCard, …)` refuses only because this
  pane's own question about this very project is up (`Probing`/`Asking` and `m_initProject ==
  project`), the request id is linked to the standing question (`m_initRequestId = id`) and
  nothing is sent — the user's click becomes the answer. The instant `accept:false` remains for
  a remembered no and a guest's pane.
- `docs/AGENT-SESSIONS-PROTOCOL.md` §19.12 documents the link.
- Commits on main: `5979eb7c` (Pane.h hunks, swept in by a concurrent session's commit),
  `06f6282e` (test), `f10b1b83` (protocol doc). land.py's verify gate built the exact landed
  tree (`--target relay`) before each swap.

## Verification

```
$ scripts/relay-build --target relay-projectinit-tests   # builds
$ ./build/relay-projectinit-tests
PASS   : ProjectInitTests::theStandingQuestionAnswersTheWorkersParkedRequest()
Totals: 20 passed, 2 failed, 0 skipped, 0 blacklisted
```

The 2 failures are pre-existing at HEAD (`thePaneRaisesEveryTriggerAndBlocksNone`,
`theWindowOffersItPassivelyAndRemembersTheAnswer` — both assert strings with zero occurrences
at HEAD; filed separately as #FV0P). The new test and every other assertion pass; the `relay`
target builds from the landed tree.
