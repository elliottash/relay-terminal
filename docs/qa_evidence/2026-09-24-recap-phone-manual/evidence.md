# #WMXN — phone Recap sends `manual`, not `remote`: evidence (2026-09-24)

## What changed

The card's plan was to change `hooks.recap` in `src/Pane.h` from
`{"type", "recap_request"}, {"reason", "remote"}` to `"manual"`. On execution
(2026-09-24) that one-word change was **already on `main`**: commit `8fc8d60b`
"recap: the phone's Recap button sends a reason the worker accepts (#WMXN, #PH0N)",
2026-09-20 18:25. No further code change was needed and none was made.

On `main` today (`git show main:src/Pane.h`, line 7306) and in the working tree
(line 7322):

```cpp
// "manual", not "remote": the worker takes three reasons — away, resume,
// manual — and refused this one, so the phone's Recap button has never
// produced a recap (#WMXN).
hooks.recap = [this] { send({{"type", "recap_request"}, {"reason", "manual"}}); };
```

## Grep: no `"remote"` recap reason survives

```
$ grep -rn 'recap_request' src/ backend/ remote/ app/ | grep -v test
src/RemoteShare.cpp:186:            {"t", "recap_request"}, {"pane", paneId(id)},
src/Pane.h:4472:        send({{"type", "recap_request"}, {"reason", "manual"}});
src/Pane.h:7322:            hooks.recap = [this] { send({{"type", "recap_request"}, {"reason", "manual"}}); };
src/Pane.h:8921:            send({{"type", "recap_request"}, {"reason", "away"}});
src/Pane.h:8938:            send({{"type", "recap_request"}, {"reason", "away"}});
backend/relay_core/session_protocol.py:494:    @_command('recap_request')
backend/relay_core/session_protocol.py:495:    def _recap_request(self, payload):

$ grep -rn '"remote"' src/ backend/ | grep -i recap      # no output
```

`src/RemoteShare.cpp:186` is the phone→GUI wire message (`{t, pane}`, no reason —
unchanged). Every reason sent to the worker is `manual` or `away`.

## Tests (run 2026-09-24, working tree at `main` + other sessions' uncommitted work;
session_protocol/suggestions/test files verified unmodified by `git status`)

Worker-side — `manual` accepted and always runs, `away` deduped
(`tests/test_session_protocol.py:351` `test_away_recap_not_repeated`, manual
assertion at :369-373):

```
$ PYTHONPATH=backend python3 -m unittest -v \
    tests.test_session_protocol.ProtocolHandlerTests.test_away_recap_not_repeated
test_away_recap_not_repeated ... ok
Ran 1 test in 0.030s  OK
```

Phone-side UI — Recap under the pane menu sends `recap_request`
(`tests/test_pane_view.py`):

```
$ PYTHONPATH=backend python3 -m unittest -v tests.test_pane_view -k recap
test_recap_is_under_the_pane_menu_and_sends_recap_request ... ok
Ran 1 test in 0.878s  OK
```

Phone-side wire/security — `recap_request` in `GuestReachTests.EVERY_PANE_MESSAGE`
(`tests/test_remote_security.py:530`; `-k recap` matches no test *name* there, so
the containing test was run):

```
$ PYTHONPATH=backend python3 -m unittest -v \
    tests.test_remote_security.GuestReachTests.test_no_message_at_all_reaches_the_pane_a_guest_was_not_invited_to
Ran 1 test in 0.058s  OK
```

## Known unrelated failure (not this card)

`tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute`
fails at its **plan_execute** tail (line 347,
`sent[-1]['content'].startswith('Execute the plan in …')` is False) — after its
recap assertions pass. Reproduced on a **clean `git archive main`** export, so it
is pre-existing on `main` and independent of this card (which changed no Python).
Already filed as **#P4XN** (bugs, 2026-09-24T04:17Z).

## Behaviour check

No paired phone was available. Per the plan's fallback, code inspection plus the
tests above stand as evidence: the only byte the fix changed is the reason string
(`8fc8d60b`, diff = 1 hunk in `src/Pane.h`), the worker's acceptance and
always-run behaviour for `manual` is asserted by
`test_away_recap_not_repeated`, and the phone-side wire is untouched and green.
