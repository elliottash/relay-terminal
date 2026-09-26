# #SSRQ MCP server support — implementer evidence

Commits: `97969b14` (feature, tests, docs) and `1f2b4c17` (Pane: "Always allow" on an MCP ask
leaves the approvals checklist alone; built on the exact landed tree by land.py's verify slot).
Reference: `docs/MCP.md`.

## How each `## Done means` line was checked

| Done means | Evidence (`tests/test_mcp.py`, output in `test_mcp.txt`) |
| --- | --- |
| Server configured globally or per project (command or URL, trust) → the pane agent's tools include its tools; a call reaches the server and returns its result | `WorkerTests.test_a_trusted_server_is_loaded_and_called_in_a_real_turn`: real `backend/worker.py` with a scripted provider. The first request's prompt names `mcp_fake (...)`, the model calls `load_tools`, the next request carries `mcp_fake_add`, and the tool result is `42`. Config merge and project enablement: `ConfigTests.*`. URL servers: `HttpTests.test_streamable_http_json_and_sse` (JSON and SSE responses, `${VAR}` header expansion). |
| Trusted runs without approval; untrusted produces an approval ask naming the server before the call is relayed | `WorkerTests.test_an_untrusted_server_asks_in_a_real_turn_and_deny_refuses`: the worker emits `question {kind: approval, capability: "mcp:fake"}` naming the server and the tool. After deny, the tool result is the refusal, the server never answers `42`, and the turn carries on. `AgentTests.test_an_untrusted_server_asks_…` covers once, always (marks trusted in config) and read-only turns. `GuestBridgeTests` shows the same ask through a guest's `relay_board` bridge. |
| Import from Claude Code, Codex and Warp shows a preview and adds only confirmed rows | `ImportTests.test_preview_then_write_only_confirmed_rows`: all three formats, statuses `new`/`same`/`conflict`/`invalid`, env key names without their values, a conflict never overwritten, file mode 0600. `ImportTests.test_cli_preview_writes_nothing`: the CLI writes nothing without `--add`. |
| A server that is down, slow or misbehaving fails its one call with a readable error — never the turn or the pane | `ClientTests.test_slow_call_times_out_and_stop_cancels`, `test_a_crash_fails_the_call_readably_and_the_next_call_restarts`, `test_a_server_that_will_not_start_or_talks_nonsense`; `HttpTests.test_unreachable_url_fails_readably`; `AgentTests.test_a_trusted_servers_tools_…` (isError, crash, then the next call works); `AgentTests.test_a_dead_server_offers_nothing_and_says_why`. |

Also checked: the worker's environment (for example provider keys) does not reach a stdio server,
and `${VAR}` references expand. With no servers configured, the tool list and system prompt are
byte-identical to before (`AgentTests.test_no_servers_changes_nothing`). A server that answers
late joins at the next turn boundary.

The whole file passed on a clean `git archive 97969b14` export: 24 passed.

## Deviations from the card's plan, and why

- **Step 6.** Guests get user servers through `relay_board`, not through their own
  `--mcp-config` / `-c mcp_servers.*`. A guest with bypass permissions would otherwise call an
  untrusted server without an ask.
- **Project `.mcp.json` servers do not launch until enabled** (`mcp_config enable`). The
  enablement is pinned to the entry's digest, following the task-plugin rule, so a cloned
  repository cannot run a command by being opened.
- **HTTP.** Streamable HTTP is implemented because the Done means says "command or URL". Legacy
  SSE and OAuth are refused with a sentence.

## Neighbouring suites

`tests/test_workspace_plugins.py`, `tests/test_guest_board_bridge.py`, `tests/test_approvals.py`
and `tests/test_tool_groups.py`: 124 passed and 3 failed. None of the three failures comes from
this change. `test_native_catalog_parity…` fails because another session added a new tool,
`scratch_release`, that is not on the bridge list. The two `FirstLaunchPaneTests` look for
`approvalRow(...)` in `RelayWindow.h`, which has moved to `RelayWindowSettings.cpp`.
