# MCP servers

Relay is an MCP **client** (card #SSRQ): servers you configure expose their tools to pane agents.
The code is `backend/relay_core/mcp_config.py` (config), `mcp_client.py` (the protocol),
`mcp_tools.py` (what the agent sees) and `mcp_import.py` (import).

## Where servers are configured

| Scope | File | Notes |
| --- | --- | --- |
| global | `$XDG_CONFIG_HOME/relay/mcp-servers.json` (`RELAY_MCP_CONFIG` overrides) | yours; mode 0600 |
| project | `<git root>/.mcp.json` | the file Claude Code reads; **not launched until enabled** |

Both use the common `mcpServers` shape (the `mcp_servers`, `servers` and `mcp.servers` wrappers
Warp accepts are read too). If both scopes define the same name, the project's entry wins:

```json
{
  "mcpServers": {
    "github": {"command": "github-mcp", "args": ["stdio"], "env": {"GITHUB_TOKEN": "${GITHUB_TOKEN}"},
               "trust": "trusted"},
    "docs":   {"url": "https://docs.example/mcp", "headers": {"Authorization": "Bearer ${DOCS_TOKEN}"}}
  }
}
```

- `command` + `args` + `env` + `cwd` (or Warp's `working_directory`) is a stdio server. It starts
  with a minimal environment (PATH, HOME, locale, XDG dirs, and similar) plus its own `env`. None
  of the worker's provider keys reach it.
- `url` + `headers` is a streamable-HTTP server. The legacy SSE transport (`"type": "sse"`) and
  OAuth are refused with a sentence; for a token, put it in `headers`.
- `${VAR}` in `command`, `args`, `env`, `cwd`, `url` and `headers` expands from Relay's environment.
  An unset variable expands to nothing.
- `trust`: `trusted` or `untrusted` (the default). `enabled: false` turns a global entry off.
  `timeout` is the per-call deadline in seconds (default 120).

Nothing logs or prints `env` or `headers` values. The listings and the import preview show key
names only.

### Project servers must be enabled

A `.mcp.json` comes from the repository, so opening a pane must not run its commands. A project
server launches only after it is enabled in the global file's `projects` block. That entry is keyed
by the project's resolved git root and pinned to a digest of the server's entry. It is the same
rule task plugins follow. A pull that changes the entry disables it again. The project's trust
comes from that enablement too: a `.mcp.json` cannot declare itself trusted.

```
PYTHONPATH=backend python3 -m relay_core.mcp_config list    --workspace .
PYTHONPATH=backend python3 -m relay_core.mcp_config enable  <server> --workspace . [--trust trusted]
PYTHONPATH=backend python3 -m relay_core.mcp_config disable <server> --workspace .
PYTHONPATH=backend python3 -m relay_core.mcp_config trust   <server> trusted|untrusted [--workspace .]
```

## What the agent sees

Each server becomes one `load_tools` group, `mcp_<server>`, with tools named
`mcp_<server>_<tool>`. These groups sit beside a task plugin's group (protocol 36). The system
prompt's one line names the group and its tools. The schemas arrive when the model loads the group
and are appended to the end of the tool list, so no cached prefix moves. With no servers
configured, the tool list and prompt stay byte-identical to before.

When a pane is configured, servers are listed in the background, and the configure waits at most
3 s for them. A server that answers later joins at the start of the next turn. A server that fails
to list offers no group. If a call names one of its tools, it gets the reason (`mcp_x_y is
unavailable: …`). The listing is retried at most once a minute.

## Trust

- **trusted**: calls run without an ask, like Relay's own tools.
- **untrusted**: before each call, the approval ask (protocol 27.6) goes up with capability
  `mcp:<server>`, naming the server, the tool and the arguments. *Once* allows that call. *For this
  turn* allows the server for the rest of the turn. *Always* sets `trust: trusted` in the server's
  config: the global entry, or the project enablement. *Deny* refuses the call, and the model is
  told not to route around the refusal. An agent that cannot draw an ask refuses an untrusted call.

A read-only turn, or a card's Discuss or Plan turn, refuses an MCP tool unless its server marks it
`readOnlyHint`. That check runs before any ask.

## Failure

Every call has a deadline and Stop cancels it (`notifications/cancelled` goes to the server). A
server that exits, hangs, sends garbage or answers `isError` fails **that one call**. The model
receives a sentence naming the server and, for a crash, the server's last stderr lines. The turn
and the pane carry on. A dead stdio server is started again on the next call. Each server's
process is kept for the worker's lifetime and reused across reconfigures, and all of them stop at
exit.

## Guests (Claude Code, Codex)

A guest reaches the same servers through its `relay_board` bridge, which dispatches through the
same `Agent._prepare/_execute`. An untrusted server therefore draws the same ask in a guest pane.
Relay does not pass user servers in the guest's own `--mcp-config` / `-c mcp_servers.*`: that
would bypass the trust rule, since a guest running with bypass permissions would call an untrusted
server without asking. The bridge's transport deadline for `mcp_*` calls is the long one, because
an ask can wait on the user.

## Import from Claude Code, Codex and Warp

```
PYTHONPATH=backend python3 -m relay_core.mcp_import [--workspace .]         # preview, writes nothing
PYTHONPATH=backend python3 -m relay_core.mcp_import --add github,docs [--trust trusted]
PYTHONPATH=backend python3 -m relay_core.mcp_import --all | --interactive
```

The importer reads `~/.claude.json`: `mcpServers`, plus the local-scope servers of `--workspace`.
It also reads `~/.codex/config.toml` (`[mcp_servers.*]`, where `bearer_token_env_var` becomes an
`Authorization: Bearer ${VAR}` header) and `~/.warp/.mcp.json`. Servers installed from Warp's
gallery live in Warp's database; add those again by hand. The preview marks each row `new`, `same`,
`conflict` or `invalid`. Only `new` rows you name are written, and a conflict is never overwritten.
Imported rows are `untrusted` unless `--trust` says otherwise.

The Options › Security UI for these settings is card #3KB7's reserved rows. It is not built yet;
the files and the two commands above are the interface for now.
