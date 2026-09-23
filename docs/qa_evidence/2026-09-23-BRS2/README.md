# BRS2 — guest tools lost on session replacement

## Cause and live evidence

This conversation's tool catalog contained no relay_board tools. Following the shell's process
ancestry reached Codex app-server PID 2252518, parent worker 1888356, Relay GUI 1888268.
The app-server was launched without any `-c mcp_servers.relay_board.*` arguments. Other
live Relay app-servers carried those overrides and had guest_board_bridge.py children.
No capability-file contents or user credentials were read from those live processes.

`guest_harness_provider.start_provider` supplied bridge configuration and guest instructions.
`resume_session`, invoked when loading a saved conversation into a guest pane, created a new
harness without either argument. Both adapters therefore started replacements without Relay's
MCP integration. The provider still held its bridge, so the prompt continued mentioning tools
that the replacement harness could not discover.

## Fix

Keep the initial instructions (including pane-specific suffixes and skill context) on the
provider. Pass them and the provider-owned bridge descriptor to the replacement harness.
Preserve the existing agent binding and bridge lifetime. Close a failed replacement without
closing the current harness or bridge.

## Verification

Used a clean export of c2a4b370's backend, tests and scripts, with only the changed provider
and regression test overlaid. Other sessions were editing Agent and its imports in the shared
checkout; those changes were excluded from this verification.

- Before fix: the new regression fails for both Claude and Codex because the replacement's
  bridge descriptor is None (`before.log`).
- After fix: two successive resumes per harness retain bridge/instructions and perform a real
  Unix-socket `tools/list` and `board_list` call against a temporary board. A failed third resume
  closes the broken replacement while leaving original discovery operational (`after.log`).
- Provider, Codex adapter, Claude adapter and bridge suites: **234 tests passed** (`suite.log`).
- No paid model calls or live-session restarts were used.

Command: `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_guest_board_bridge`

The running worker imported the older code; restarting Relay is required to pick up this fix.
The already-running app-server cannot gain the missing configuration from editing Python files.

Codex background reference: [official app-server documentation](https://learn.chatgpt.com/docs/app-server).
The root cause and verification above come from Relay's code, process arguments and local tests.
