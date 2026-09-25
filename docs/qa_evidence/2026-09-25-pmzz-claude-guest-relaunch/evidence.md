# #PMZZ — claude guest dies on model switch + auto set_effort relaunch

Commit: `0e52c2ddde75af7a0a75cc943bbbce56f103b3ff` on `main`
(backend/relay_core/guest_harness_claude.py, tests/test_guest_harness_claude.py)

## What the incident was

`worker.log` 2026-09-25 19:15, pane 22f05421: a model pick (kimi k3 → opus) resumed
the claude session (`--resume 922fd7e8…`), then the effort that rides the pick
relaunched the CLI before any turn had run — pinned with `--session-id 922fd7e8…`
instead of `--resume`. Claude refused (`Error: Session ID 922fd7e8… is already in
use.`), the harness closed, and every later prompt errored `the guest is not
running.` until the user gave up and switched to glm-5.3.

## The fix

- `start()` seeds `_resumable` from the resume it was given, so a relaunch before
  the first turn resumes instead of pinning the id.
- `send()` / `set_model()` / `set_effort()` on a closed harness bring it back on
  the session it had instead of raising.

## Rerun

```sh
PYTHONPATH=backend python3 -m pytest tests/test_guest_harness_claude.py -q
# 83 passed

PYTHONPATH=backend python3 -m pytest tests/test_guest_harness_provider.py \
  tests/test_guest_harness_codex.py tests/test_guest_harness_steer.py -q
# 170 passed
```

## Against the pre-fix code (land.py snapshot)

The five new tests, run against the pre-fix `guest_harness_claude.py`:

```sh
PYTHONPATH=<scratch>/backend python3 -m pytest tests/test_guest_harness_claude.py -q \
  -k "resumed_session or closed_harness"
# 5 failed, 78 deselected
#   SetModelTest::test_a_restart_before_the_first_turn_of_a_resumed_session_resumes_it
#   SetModelTest::test_set_model_on_a_closed_harness_restarts_it_on_the_session
#   SetEffortTest::test_a_send_on_a_closed_harness_brings_it_back_on_its_session
#   SetEffortTest::test_an_effort_change_on_a_resumed_session_before_its_first_turn_resumes_it
#   SetEffortTest::test_set_effort_on_a_closed_harness_rides_the_next_start
```

Two fail on the `--session-id`-instead-of-`--resume` decision, three on
`HarnessError: the guest is not running.` — the pane's exact behaviour.
