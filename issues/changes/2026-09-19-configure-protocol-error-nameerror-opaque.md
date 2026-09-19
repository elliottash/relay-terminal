---
id: 4OSN
type: work
status: open
labels: [bug]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzz110
created: '2026-09-19'
acceptance: a pane whose configure dies on an unexpected worker exception shows the exception's own message (e.g. "name 'os' is not defined"), recovers without being closed once the backend file is repaired, and a test covers the reporting and the recovery
source: 'conversation, 2026-09-19: "im getting this bug, protocol error (nameerror) when im trying to work in a new pane"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A failed configure shows only "Protocol error (NameError)." and the pane never recovers

## Issue

"im getting this bug, protocol error (nameerror) when im trying to work in a new pane"

## What happened (2026-09-19, 04:43–04:45 UTC)

New panes (`99394410`, `612b49a7`) failed their first `configure` with `Protocol error (NameError).`
eleven times, ~every 5–20 s, until the panes were given up on. `worker.log` has what the pane does not:

```
protocol_error kind=configure error=NameError msg="name 'os' is not defined"
```

Cause of the NameError itself: the backend tree was mid-edit by a parallel agent session (an
`edit_file` on pane `5b9e3f00` completed 19 ms before the first failure; `backend/relay_core/agent.py`
was saved 04:39:33, `provider.py` again at 04:47:34, after which the failures stopped). A pane worker
that starts while a module is transiently broken imports that module and then fails **every** retry of
`configure` in that process — Python never re-reads the file, so the pane stays broken even after the
tree is fixed, until the pane is closed. The same shape occurred 2026-09-18 19:04 UTC
(`name 'sessions_usage' is not defined`, 6 hits).

The current tree is clean: `configure` run end-to-end against the real worker succeeds (kimi and
openrouter presets), and every backend module compiles. Nothing is left to fix in the code that raised.

## The two real gaps

1. **The message is thrown away.** `worker.py`'s handler emits the exception text only for
   `ValueError`/`OSError`/`KeystoreError` — anything else is redacted to
   `f"Protocol error ({type(exc).__name__})."` so prompt text can never leak. But a `NameError`,
   `AttributeError` or `TypeError` message ("name 'os' is not defined") names a code defect, carries
   no user text, and is exactly what the person at the pane needs in order to report it. It is already
   in `worker.log`; the pane just refuses to show it.
2. **No process-level recovery.** The GUI retries `configure` in the same worker forever. For an
   unexpected exception the worker process is the broken unit (cached import); the only fix is a
   restart, which the GUI already knows how to do when a worker dies.

## Tasks

- [ ] `worker.py`: include `str(exc)` in the `error` event for unexpected exception types whose
      messages are code, not prompts (`NameError`, `AttributeError`, `TypeError`, `ImportError`),
      keeping the redaction for `ValueError`/`OSError`/`KeystoreError`; keep `worker.log` as the full
      record
- [ ] `src/Pane.h`: on a configure error of that unexpected kind, surface the message in the status
      line (attributed, like the #308N suggestion line) instead of the bare "Protocol error (…)"
- [ ] `src/Pane.h`: after a configure failure of that kind, restart the pane's worker (or restart
      after N consecutive configure failures) so a repaired tree is picked up without closing the pane
- [ ] `docs/AGENT-SESSIONS-PROTOCOL.md`: document the error-event shape for unexpected exceptions
- [ ] Test: a worker whose configure path raises `NameError` reports the message (not the redacted
      line) and the pane retries configure in a fresh worker process, which then succeeds

## Decisions

- 2026-09-19, agent: filed from a live incident; the redaction is kept for the three prompt-carrying
  types and widened only for exception types whose message is a code symbol — not a blanket unmask.
