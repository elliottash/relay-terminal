# No context lost on a model change (#1V4F)

Owner, 2026-09-22: "of course: its critical that no context is loss on model changes. so you can verify that".

## What was lost (measured at 18dd9c71 / 607cc093, before the fix)

`repro.py` holds one turn on a kimi endpoint ("Remember the codeword PELICAN-42"), switches the
pane to `guest:claude` exactly as `SessionCommands.switch_model` does, and asks for the codeword on
the scripted FakeHarness (no real guest is started):

```
relay transcript keeps it: True
guest was sent the earlier conversation: False
```

With the fix:

```
relay transcript keeps it: True
guest was sent the earlier conversation: True
```

The reverse direction was lost too: a guest pane's tool calls and results never entered
`agent.messages`, so a model switched in afterwards saw the guest's final prose but not what it read
or ran (only subagent children recorded them, `record_guest_tools`).

## Every switch direction

`tests/test_guest_handover.py` (9 tests): endpoint→guest (briefed once, prompt last, status line,
Relay transcript untouched), a pane that starts on a guest (never briefed), a resumed guest session
(not briefed), same guest on another model (live session kept, not re-briefed), guest→endpoint (the
guest's tool call and result reach the new model), a recorded result is capped and valid JSON,
guest→guest (the first guest's tool results reach the second), switching back to a guest after
another model ran turns (briefed on those turns), endpoint→endpoint (whole conversation kept).

- `tests-at-parent.txt`: the same file run against the parent tree — 6 of 9 fail (the 3 that pass
  guard behaviour that was already right).
- `tests-with-fix.txt`: all 9 pass.

Reproduce: `PYTHONPATH=backend:.:tests python3 -m unittest -v tests.test_guest_handover` and
`PYTHONPATH=backend:.:tests python3 docs/qa_evidence/2026-09-22-no-context-lost-on-switch/repro.py`.

## Bounds (not losses the fix can remove)

- The handover brief is capped at 160 KB of text, dropping the oldest part first with an
  "earlier conversation omitted" line; tool results in it are cut to 4 KB each.
- A switch to a model whose window is smaller than the conversation compacts first (unchanged):
  that summary is lossy by nature, and a conversation that cannot fit at all is refused.
