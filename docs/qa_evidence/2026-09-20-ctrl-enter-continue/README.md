# Ctrl+Enter continues a stopped turn (#SXF1) — implementer evidence

Live check of the landed build at tip `c0af1554` (the card's commits `d9cde605`,
`472ae1a2`, `78416e39` plus everything after), 2026-09-20 ~01:32 local, under
`Xvfb :163` with an isolated `XDG_CONFIG_HOME` whose only provider is a mock
OpenAI-compatible server on loopback (`drive.py`; no network, no real key,
`agent/max_steps=2` so a turn hits the step limit in two model calls).

```
PASS window came up
PASS turn ran to its step limit (2 model calls)
PASS A: Ctrl+Enter on the empty box sent the prompt "Continue"
PASS relaunched after kill -9
PASS B: session restored ("Session loaded" line)
PASS B: Ctrl+Enter on the empty box sent "Continue" after the cut-off restart
PASS C: /continue shows the "Next time: Ctrl+Enter" hint
      — "Next time: Ctrl+Return · continue a stopped turn from an empty prompt box"
ALL PASS
```

## What each check is

- **A — the step-limit stop** (`implementer-01..03`): a turn runs into
  `done {stop_reason: "limit"}`; with the prompt box empty, Ctrl+Enter sends the
  prompt `Continue` (the mock sees a user message ending in `Continue`, and
  `worker.log` shows the `turn_start` for it). No "Type a prompt first." status.
- **B — the cut-off restart** (`implementer-04..06`): a turn is left mid-flight
  (its second model call never answers after one completed tool step), Relay is
  `kill -9`ed, relaunched; the layout autosave resumes the session
  ("Session loaded" line), and Ctrl+Enter on the empty box sends `Continue`
  again — the worker had reported `state_loaded {turn_open: true}`, because the
  hung turn's checkpoint was written by the mid-turn autosave without an `ended`
  stamp.
- **C — the slow path teaches the fast one** (`implementer-07`): `/continue`
  shows the Superhuman-style hint naming the empty-box key, built live from
  `agent.interrupt`'s first binding — which the Keymap spells `Ctrl+Return`
  (Return and Enter are the same key; the card's title says Ctrl+Enter). The
  profile's persisted `[hints]` section records `continue.slow=1`.

Also visible in the shots: the ▸ Continue link now teaches
`Ctrl+Return or /continue`, and the palette row (not shot) matches.

## Harness notes (why the drive is shaped as it is)

- The hung turn in B first completes **one tool step**: the session's mid-turn
  autosave fires at step boundaries only, so a turn killed on its first model
  call is never written to disk at all — `turn_open` is then rightly `false`
  (nothing to continue) and Ctrl+Enter correctly says "Type a prompt first.".
  Same for a turn stopped with Esc: its `ended` is stamped, no continue offered.
- Before the kill, the drive waits out the autosave's 10 s throttle, so the
  hung turn's first step boundary really saves it.
- In C, `/continue` is typed early (a non-empty editor stops the 4 s idle-tip
  timer) and submitted >20 s after the limit line printed: the limit line's own
  hints (`turn.link`, `call.fold`) open ShortcutHints' global 20 s gap, which
  would otherwise silently drop the `continue.slow` hint at `Pane::hint`'s
  `mayShow` gate.
- The mock hangs the `/continue` turn so its turn-end status cannot retire the
  5 s toast before the screenshot.
- `wait_for` runs its predicate on a snapshot outside the lock:
  `seen_continue_after_marker` takes the lock itself, and calling it under the
  lock deadlocks (found the hard way — it froze the mock's handler threads and
  stalled the agent's turn).

## Targeted tests (current tree, this session)

- `ctest --test-dir build -R continueturn` — 1/1 passed.
- `python3 -m unittest tests.test_conv_index.HelperTests.test_turn_left_open_reads_only_the_checkpoint_stamps
  tests.test_conv_index.HelperTests.test_unfinished_reads_checkpoints_messages_and_todos
  tests.test_sessions.SessionTests.test_resume_reports_a_turn_left_open` — 3/3 OK.

## Owner reports during execution ("still says type a prompt first")

The instance the owner typed in (pid 118616) was started 2026-09-19 15:54,
hours before the three commits landed (23:39–23:48) — it predates the feature.
After a restart of Relay the empty-box Ctrl+Enter continues. By design it does
*not* continue a turn that finished normally, or one that was stopped with Esc,
or one that died before its first autosave: those keep "Type a prompt first.".

## Files

- `drive.py` — the whole run (mock server, profile isolation, X11 driving, OCR
  assertions). Reproduce: `Xvfb :163 -screen 0 1400x900x24 &` then
  `DISPLAY=:163 python3 drive.py <path-to-relay>`.
- `implementer-*.png` — one shot per step, numbered in story order.
- `mockserver.log` — every model request the mock saw (JSON list) plus its own
  access log; the `Continue`-ending user messages are the A/B assertions' raw
  evidence.
- `relay.log`, `worker.log`, `relay.out` — the app's own logs for the run.
