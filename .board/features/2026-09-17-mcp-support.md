---
id: SSRQ
type: work
status: needs-verification
labels: [feature]
component: [worker]
milestone: post-mvp
workstream: agent
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 52e7f666-1bc2-4999-b7c2-37a733bcea9a
rank: '58'
created: '2026-09-17'
acceptance: a configured MCP server's tools are available to pane agents
source: '`issues/feature_intake.txt`, 2026-09-17: "MCPs?"'
links: {plans: [], commits: [97969b14a7c3, 1f2b4c17eca1, 9982be5e225a], evidence: [docs/qa_evidence/2026-09-25-ssrq-mcp-support/], related: [], github: null}
---
# MCP server support

## Notes
Possibly configured in the global Switchboard (TASKS-AND-MEMORY-DESIGN.md section 9, decision 6).

## Open questions
1. In scope for the MVP, or after?
2. Agent tools run without approval (owner decision); should MCP tools also, or per-server trust levels?
3. Import server configs from Claude Code, Codex and Warp?
4. Global only, or also per project (like `.mcp.json`)?

## Decisions (owner, 2026-09-17)
All recommendations accepted: after the MVP; trust level per server (trusted run freely, untrusted ask first); import
configs from Claude Code, Codex and Warp with a preview; configured globally (global Switchboard) and per project.

## Done means
With an MCP server configured globally or per project (command or URL, trust level), a pane agent's tools include that server's tools, and calling one reaches the server and returns its result. A trusted server's tools run without approval; an untrusted one's produce an approval ask naming the server before the call is relayed. Import from Claude Code, Codex and Warp shows a preview and adds only confirmed rows. A server that is down, slow or misbehaving fails its one tool call with a readable error — never the turn or the pane.

## Plan
**Goal.** Relay speaks MCP as a client: servers configured globally and per project expose their tools to pane agents, gated by per-server trust (trusted runs freely, untrusted asks first), with import-with-preview from Claude Code, Codex and Warp — the owner's four decisions of 2026-09-17, and the card's acceptance.

**Findings.**
- Pane tool assembly is `backend/relay_core/agent.py` around lines 1486–1527: `prompt_profiles.tool_specs(offered)` plus extras (subagents :1510, side panes :1516, board :1521, plugins via `_with_plugin_tools` :1527). MCP tools attach here.
- `backend/relay_core/workspace_plugins.py:828` `PluginTools` (`specs()` :854) is the existing pattern for an external tool group: launch with a filtered env, list specs, execute, defer to `load_tools` (protocol 12.13) so a big server does not bloat every prompt.
- Dispatch lives in `backend/relay_core/tools.py` `ToolExecutor` (:547; `tools()` :681, `prepare()` :704, `execute()` :1185).
- Approvals: `backend/relay_core/approvals.py` `Policy` (:71) and `needed()` (:164) are where an untrusted server's ask goes; session grant caching already exists there.
- The guest harnesses already carry MCP server config: `guest_harness_claude.py:363` passes `--mcp-config` and `guest_harness_codex.py:291,637` `-c mcp_servers.relay_board.*`. Merging user servers beside `relay_board` is mechanical.
- `backend/relay_core/guest_board_bridge.py` speaks MCP server-side (stdio JSON-RPC framing), so no SDK is needed for the client either.
- Global config home: `task_plugins.global_dir()` (task_plugins.py:969); the design doc (TASKS-AND-MEMORY-DESIGN.md:421) puts MCP server config in the global Switchboard. Options › Security reserved rows for it (#3KB7).

**Steps.**
1. `backend/relay_core/mcp_client.py` — a minimal MCP client over stdio JSON-RPC (`initialize`, `tools/list`, `tools/call`; launch the server as a subprocess with a filtered env; per-call timeout and cancel; restart on demand). Mirror the framing `guest_board_bridge.py` already speaks server-side; no third-party dependency.
2. `backend/relay_core/mcp_config.py` — merged view of global (`<global_dir>/mcp-servers.json`) and project `.mcp.json` (project wins on name collision). Validate shape `{command|url, args, env, trust: trusted|untrusted, enabled}`. Never log server `env`.
3. `McpTools` beside `PluginTools` — `tool_specs()` with namespaced names (`mcp_<server>_<tool>`) and an `execute()`; attach in `agent.py`'s spec assembly next to the other extras, offered as a `load_tools` deferred group.
4. Route in `ToolExecutor.prepare`/`execute` (tools.py:704/:1185): `mcp_*` tools go to `McpTools`; results pass through the existing model-result clipping.
5. Trust in `approvals.needed()` (approvals.py:164): an untrusted server's tool returns an approval ask naming the server; trusted runs without; the grant is cached for the session like existing approvals.
6. Guests: merge user-configured servers into `--mcp-config` (guest_harness_claude.py:363) and `-c mcp_servers.*` (guest_harness_codex.py:291) beside `relay_board`.
7. `backend/relay_core/mcp_import.py` — read Claude Code `~/.claude.json` (`mcpServers`), Codex `~/.codex/config.toml` (`mcp_servers`) and Warp's config; print a preview table; write only confirmed rows into the global file. CLI subcommand is enough; the Options › Security UI is #3KB7's reserved rows and stays separate.

**Risks.** Stdio only at first — HTTP/SSE servers are out until asked for (owner to confirm). A hung server must never hold the turn: timeouts and per-call cancel are part of step 1, not polish. Server `env` blocks contain secrets: pass them to the subprocess, never into logs or errors. GUI settings UI is deliberately not in this card.

**Verify.** Unit tests for config merge/validation and the import preview; a fake MCP server script in `tests/` used to check: specs appear on the pane, a call returns its result, an untrusted server triggers the ask, a dead server fails one call without ending the turn; an argv test that guest harnesses carry user servers beside `relay_board`. Run the targeted pytest/ctest subset per the repo's targeted-tests rule.

**Orchestration.** One agent, sequential: steps 1–2, then 3–5 (one reviewable change), then 6–7. No parallel block — steps share `agent.py`/`tools.py` surfaces.

## Execution Summary
Relay is now an MCP client (`docs/MCP.md`).

- `backend/relay_core/mcp_config.py`: merges the global `$XDG_CONFIG_HOME/relay/mcp-servers.json` with the project `<git root>/.mcp.json`; the project wins on a shared name. A project server launches only after `python3 -m relay_core.mcp_config enable <server>`, which pins it to a digest of its entry, and its trust comes from that enablement. The module validates entries, expands `${VAR}` references, and never prints `env` or `headers` values.
- `mcp_client.py`: stdio and streamable-HTTP transports with no third-party dependency. Every call has a deadline, and Stop sends `notifications/cancelled`. A stdio server gets a filtered environment. A crashed stdio server restarts on the next call, and a failure names the server and quotes its last stderr lines.
- `mcp_tools.py`: each server becomes one `load_tools` group, `mcp_<server>`, beside task-plugin groups. Servers are listed in the background (configure waits at most 3 s), and a server that answers late joins at the next turn boundary. A trusted server runs without an ask. An untrusted server raises the protocol 27.6 ask with capability `mcp:<server>`: once, for this turn, always (which writes `trust: trusted` into its config), or deny. Read-only and card turns refuse a tool unless its server marks it `readOnlyHint`.
- Wiring: `agent.py` (`_plugin_groups`, `_prepare`, `_execute`, the turn-start refresh), `worker.py` (attach on configure), `approvals.py` (labels for `mcp:` capabilities), `guest_board_bridge.py` (guests get the same tools and the same ask; `mcp_*` calls use the long transport deadline), and `src/Pane.h` ("Always allow" on an `mcp:` ask no longer rewrites the approvals checklist or the first-launch choice).
- `mcp_import.py`: previews servers from Claude Code (`~/.claude.json`, including the workspace's local scope), Codex (`~/.codex/config.toml`) and Warp (`~/.warp/.mcp.json`). Rows are marked new, same, conflict or invalid. It writes only rows named with `--add`, `--all` or `--interactive`, never overwrites a conflict, and imports rows as untrusted by default.

Deviations from the plan: guests reach servers through `relay_board` rather than their own `--mcp-config` (step 6), because that would bypass trust. Project servers need enabling. Streamable HTTP was added because Done means says "command or URL"; legacy SSE and OAuth are refused with a sentence. Not built: the Options › Security UI, which stays with #3KB7.

## Tests
- `python3 -m pytest tests/test_mcp.py`: 24 passed, and 24 passed again on a clean `git archive 97969b14` export (`docs/qa_evidence/2026-09-25-ssrq-mcp-support/test_mcp.txt`). It covers config merge and enablement, secret redaction, stdio and HTTP clients, timeout, Stop, crash and restart, bad startup, agent loading and trust, the guest bridge ask, import, and two real `worker.py` turns (trusted call returns 42; untrusted asks with `mcp:fake`, and deny refuses while the turn carries on).
- `src/Pane.h` guard: land.py built the exact landed tree in its verify slot (`--target relay`).
- Neighbouring suites `test_workspace_plugins`, `test_guest_board_bridge`, `test_approvals`, `test_tool_groups`: 124 passed and 3 failed. None of the failures comes from this change: a new `scratch_release` tool from another session is missing from the bridge parity list, and `approvalRow` moved to `RelayWindowSettings.cpp`.
