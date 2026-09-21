# #H6VQ — the Sessions helper opens conversations, says so, and can be interrupted

Implementer evidence, 2026-09-20. Everything here was produced on this machine with **no provider
account**: an isolated `HOME`/`XDG_*`/`TMPDIR` under a short path, `RELAY_KEYRING=off`, and a
loopback OpenAI-compatible stub (`stub-provider.py`) as the only model. The owner's own logs were
read but never written to.

## What was wrong

> "sessions helper didn't do anything when I asked to open a group of previous sessions in new
> panes." — owner, 2026-09-20 23:22

Two faults, one on each side of the pipe.

1. **The GUI never answered the helper's `app_command`.** `RelayWindow::boardWorker`'s `onEvent`
   read the event's name out of `type`; a worker names its events in `event` (`type` is what the
   GUI calls the messages it sends *down*). It had said `type` since the helper route was written
   (c78c8004, the same day), so the branch matched nothing and every `app_open`, `app_option_set`,
   `app_action_run` and `app_undo` a tab's helper sent went unanswered.
2. **There was no tool for what he asked.** `OPEN_TARGETS` could open the Sessions *list* at a
   search and nothing else; the Sessions row's Enter — `SessionManager::onResume` →
   `Pane::openSavedSession` — was reachable by hand and by no agent.

In his run the two met: the helper searched (answered inside the worker, so it worked), called
`app_open`, nothing answered, and five seconds later the tab it had been asked in closed, which
stops a tab's helper (§30.7) and killed the pending call — `app_open … ok=False ms=5358` in the
same millisecond as `worker_stop`, and a turn that never answered.

## `repro.py` — the fault and the fix, through the real worker

The worker is `backend/worker.py`; the "GUI" is ten lines that answer `app_command`. The two modes
differ in **one** line — which key the fake GUI reads the event's name out of — so the difference
in the output is the bug and nothing else.

| run | log | what it shows |
|---|---|---|
| `repro.py before` | `repro-before.log` | `tool=app_open ok=False ms=20030` — the 20-second deadline, "Relay did not answer the open request within 20s". Nothing was opened. |
| `repro.py before --tab-close` | `repro-before-tabclose.log` | the owner's own sequence: the tab closes at 5 s with the call in flight, `worker_stop`, and **no tool result and no answer at all** |
| `repro.py after` | `repro-after.log` | three `app_command`s answered, one per id, in the order given; `tool=app_open ok=True ms=1`; and the panel line the model never wrote |

## `drive.sh` — the live run (12 PASS, 0 FAIL: `notes.txt`)

Three saved conversations are seeded into the isolated data root, under the workspace-digest
folder `SessionStore` writes to (which is the shape `conv_index.reconcile()` walks).

- **01-sessions-helper.png** — the Sessions pane with its helper panel expanded.
- **02-answer.png** — the ask ("open the three sessions about panes in new panes") and the
  answer: *"Opened 3 conversations in new panes: Splitting the pane layout, The pane header and
  its labels, Pane drag and the equalize key."* The stub deliberately answers that turn with **no
  prose at all** — this line is the worker's "never answer with nothing after an app call" floor,
  built from the tool's own result (owner: "it also needs to reply in text that it is doing it").
- **03-three-panes.png** — the three new panes beside the Sessions pane, each headed with its
  conversation's title and each saying "Session loaded: …". This is the thing that could not be
  asked for before.
- **worker-app-calls.log** — `tool=app_sessions_search ok=True`, then `tool=app_open ok=True
  ms=109`: the GUI answered, in milliseconds, where it used to answer never.
- **worker.log** — no `worker_stop` anywhere in the run: opening three panes does not stop the
  tab's helper (§30.7).
- **05-running.png / 06-queued.png** — a second ask while a turn runs: the Sessions panel now
  draws the queue — "1 prompt waiting", the row with its ×, and the busy strip with ✕ Stop. Until
  this card the queue box was built only for the Switchboard, so in Options, Actions and Sessions
  the composer promised that "a second prompt queues" and then showed nothing.
- **07-stopped.png** — ✕ Stop pressed in the Sessions panel: "Stopping the page agent…", the
  interrupted turn marked `(cancelled)`, and the prompt that was waiting now running. Stopping
  ends the turn; the queue survives it (19.18).

## What is *not* shown here

The before-fix GUI screenshots. Reproducing the fault in the window means building the old binary,
and the same fault is shown end to end through the real worker in `repro-before*.log`, against the
owner's own `worker.log` lines quoted above. The GUI run is the after.
