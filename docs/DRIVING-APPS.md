# Build apps so people and agents can drive them

Human tester time is the scarce resource. Before staging a change, number the user's path and
mark each step `check` (an assertion decides), `agent` (play and capture), or `person` (judgement).
Record the three counts, explain why every person step needs a person, and estimate their minutes.
Play all mechanical steps first. Keep expected results sealed until the person has answered.

Give each interactive control a stable, unique name independent of wording or layout. Expose
open, press, read and type operations that use the same event handlers as a click or keystroke.
Refuse missing, ambiguous, hidden, disabled and read-only targets explicitly. Read the rendered
state, then assert the outcome; an accepted click alone is not evidence of completion. Never
silently fall back to screen coordinates. Keep fixtures disposable, isolated and rerunnable.

## Relay's local driver

`scripts/relay-drive` uses the existing private local open socket (same user only). Select a
specific instance with `--socket PATH` or `RELAY_OPEN_SOCKET`; otherwise it reads the isolated
profile's `$XDG_RUNTIME_DIR/relay/open-socket`. The command-line client uses Unix sockets on
Linux/macOS; the GUI protocol is also carried by Qt's local pipe on Windows.

```
scripts/relay-drive open K7Q2
scripts/relay-drive read sections
scripts/relay-drive press boardTestsCheck
scripts/relay-drive read notice
scripts/relay-drive type boardFilter K7Q2
scripts/relay-drive action tests.open
scripts/relay-drive panes
```

Responses are JSON with `ok`; refusals add `error` and exit nonzero. Open and press may start
asynchronous work: poll the resulting read until the expected state appears, with a deadline.
The card must finish loading before its sections or controls can be read. `action` delegates to
the same action registry and policy as `app_action_run`, with optional `--pane TOKEN`; `panes`
uses the same pane model. Board operations target the Board on the active window's current tab.
For deterministic QA, launch one isolated window per fixture.

Control names include `boardFilter`, `boardCardTitleEdit`, `boardIssueEditor`, `boardTestsEditor`,
`boardQuickAdd`, `boardTestsCheck`, `boardCardDone`, `boardTryOpen`, `boardTryAnswer`, `boardTests`
and `boardProfile`. Menu entries use `profileTarget:build`, `profileTarget:tests`, etc., while
the menu is open. `read sections` returns the actual rendered card document; `read notice`
returns the visible notice. `read NAME` reports a named control's text and enabled state.
Typing is restricted to the five named Board editing fields above; terminal and agent-console
input is not exposed. Text replaces the selected field through its normal text-change signals.

The socket accepts one newline-delimited object with `type: "drive"`, `op`, and optional `name`,
`card`, `text`, `pane`, and returns one JSON object. Requests must be smaller than 64 KiB.
The socket is local control of the app, not a remote service. Do not publish it to other users.

A worked fixture is `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh`.
Use a private Xvfb display and HOME/XDG/TMPDIR; capture each important state, not just the final
screen. Hand the person the prepared situation, one task and one judgement question.
