# Card `## Tests` strip, Check, and the needs-verification gate — live proof

Card `#7BM4` phase 4 (task `t:q5`), 2026-09-20.

Driven under Xvfb `:77` with everything isolated (its own `XDG_*` and `TMPDIR` under
`/tmp/claude-1000/ct/x`, `RELAY_KEYRING=off`, `/run/user/1000/bus` symlinked in,
`instructions/onboarded` and `security/approvals_chosen` pre-set), against a throwaway CMake
project at `/tmp/claude-1000/ct/ws` with a Switchboard and three ctest tests — `alpha` and
`beta` pass, `gamma` fails — plus a run history in which `alpha` passed twice and `gamma`
failed twice. Card `#T1ST` sits in `needs-verification` and its `## Tests` names
`ctest -R alpha`, `ctest -R gamma` and `ctest -R vanished` (which does not exist).

| Shot | What it shows |
|---|---|
| `implementer-01-strip.png` | The card open. Above the body, where the verify line sits: **Tests 3 listed · never checked**, with the **Check** button at the right. |
| `implementer-02-findings.png` | After one press of Check. Two findings as rows — `failure · ctest:vanished` (not in the project any more) and `failure · ctest:gamma` (failed the last time it ran, with the timestamp) — and the one action the worker offered, **Open the failing one**. The worker has written `### Check 2026-09-20 17:16` under `## Tests` in the card body, and the thread carries its `evidence` entry. |
| `implementer-03-gate.png` | The status picker moved to **Done**. The worker refused: the notice says *"#T1ST still has 2 test(s) that do not prove it (ctest:vanished, ctest:gamma): run or fix them, or move it with an override that says why."*, with **Override…** beside it. The picker has snapped back to *Needs verification*, because nothing was written. |
| `implementer-04-override.png` | **Override…** asks for the reason in one line. |

After the reason was typed and accepted, the same move went again with `override` on it: the
card became `status: done` (its file moved to `issues/features/done/`) and its thread gained

```
<!-- relay:entry 20260920T211732Z-xl author=owner kind=decision -->
Moved to `done` with the Check gate overridden: "the vanished test moved to the suite next door; gamma is tracked on its own card"
```

The card with **no** `## Tests` section (`#N0TS`, in Executing) shows no strip at all, and is
not gated.

Headless tests for the same behaviour: `ctest -R cardtests` (9 cases) and
`tests/test_tests_protocol.py` (91 cases).
