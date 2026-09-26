# Verify PBKR — Plan restores the selection (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). Commit ef706804 is an ancestor.

## Tests
- `XDG_CONFIG_HOME=$(mktemp -d) xvfb-run -a ./build/relay-consolemode-tests --plan-click-only` — **exit 0, silent = all three plan cases green** (`enteringPlanSelectsHigh`, `clickingPlanLeavesModeAndPreservesDraft`, `planWhileConfiguringSelectsHighBeforeMode` — tests/consolemode_test.cpp:2000-2005 return 1 on any failure).
- `PYTHONPATH=backend python3 -m unittest tests.test_guest_handover` — **OK**.
- Build of relay-consolemode-tests at HEAD — green.

## Done means
- Leaving Plan restores role/model/effort; repeated Plan entries don't overwrite the saved selection; manual/worker-reported exits and entering before configuration all preserve selection and conversation — **passed by test evidence**: the three consolemode cases + the guest-handover suite cover exactly those paths at HEAD. Live re-drive not repeated (the implementer's README carries real pane screenshots with scripted worker events — present).

## Verdict
PASS.
