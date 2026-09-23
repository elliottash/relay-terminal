# Simplified subagents — implementer evidence

Built-in explore is retired; general handles investigation with task-specific instructions.
Explicit custom read-only definitions remain restricted. Signal repair remains unchanged.
Tracker summaries no longer show role prefixes, including saved explore and signal rows.

Children are instructed to begin final reports with `Status: completed` or `Status: blocked`.
The explicit blocked marker produces blocked subagent status, persists in the thread record,
returns through agent_wait, and leaves the linked todo blocked with its reason. A follow-up can
resume and complete it. Unmarked reports keep the previous done behavior for compatibility;
Relay does not guess completion from incidental prose. Existing saved outcomes are not rewritten.

## Validation

Backend checks ran on a clean export of c2a4b3700fec with this patch overlaid, because concurrent
terminal-context edits in the shared checkout temporarily broke agent initialization and bridge
schemas. No other session's files were changed to make tests pass. Two older guest-delegation
assertions assumed the bridge exposed no remote tools; they now check delegation capabilities
specifically, including no nested delegation.

105 tests passed (`backend-tests.txt`):

```bash
PYTHONPATH=backend:tests python3 -m unittest tests.test_agents_defs tests.test_subagents tests.test_guest_delegation tests.test_todo_subagents tests.test_signal_threads tests.test_approvals.SubagentApprovalTests tests.test_failover.SubagentFailoverTests tests.test_tier_lists.SubagentTests -q
```

Targeted Qt build and both suites passed (`qt-tests.txt`):

```bash
scripts/relay-build --target relay-subagents-tests relay-striplayout-tests
ctest --test-dir build -R '^(subagents|striplayout)$' --output-on-failure
```

Xvfb with isolated XDG_CONFIG_HOME passed `trackerOmitsRoleTagsIncludingHistoricalRows` and
`blockedReportIsNotSuccessfulCompletion`. `tracker.png` was inspected: a1/a2/a3 have unprefixed
summaries; a2 has a red exclamation mark and blocked status, while successful rows show checks.
The render test compares otherwise identical general and historical explore/signal rows pixel
for pixel, so adding a role prefix again fails the test.

The main application also built successfully with `scripts/relay-build --target relay`
(build 2026-09-22.20H.08). The board's tests_check equivalent resolved all three evidence paths.
The repository-wide board check reported 12 errors and 754 warnings in other cards/threads,
with no findings for SBGN.
