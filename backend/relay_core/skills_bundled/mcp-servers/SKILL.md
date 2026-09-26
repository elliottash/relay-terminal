---
name: mcp-servers
description: Add, import (Claude Code, Codex, Warp), enable or trust MCP servers for pane agents. Preview first; trust or enable only on the user's yes.
short: 'Set up MCP servers: add, import from Claude Code/Codex/Warp, enable, trust.'
---

# MCP servers

Relay is an MCP client (card #SSRQ). Servers configured here expose their tools to pane agents
as `load_tools` groups named `mcp_<server>`. Options › Security › MCP servers is the same thing
with buttons (card #9M96); this skill is for walking a user through it in a pane. `docs/MCP.md`
in a Relay checkout is the full reference.

## The command

Everything goes through two CLIs. Never edit the config files by hand: the CLI validates the
entry, holds the lock, keeps the global file at mode 0600 and pins project enablements to a
digest.

```bash
B=<Relay's backend folder>   # three levels above this SKILL.md; `backend/` in a Relay checkout
PYTHONPATH=$B python3 -m relay_core.mcp_config list --workspace .      # what is configured now
PYTHONPATH=$B python3 -m relay_core.mcp_import --workspace .           # import preview, writes nothing
```

`list` prints server, scope, trust, transport and command/URL. It shows env and header **names**
only, never their values. Keep it that way in what you tell the user.

## Adding a server

A stdio server is a command; a remote one is a streamable-HTTP URL. Pass each argument as
`--arg=VALUE` (with the `=`: a value starting with `-` is otherwise taken as an option). Secrets go
on **stdin** with `--secrets-stdin`, never in argv, where anyone on the machine can read them:

```bash
printf '%s' '{"env": {"GITHUB_TOKEN": "${GITHUB_TOKEN}"}}' | PYTHONPATH=$B python3 -m relay_core.mcp_config \
    add github --command=npx --arg=-y --arg=@modelcontextprotocol/server-github --secrets-stdin
PYTHONPATH=$B python3 -m relay_core.mcp_config add docs --url=https://docs.example/mcp \
    --secrets-stdin <<< '{"headers": {"Authorization": "Bearer ${DOCS_TOKEN}"}}'
```

Prefer a `${VAR}` reference over the literal token: it expands from Relay's environment when the
server starts, so the secret never sits in the file. The legacy SSE transport and OAuth-only
servers are refused; say so, and look for the server's stdio command or streamable-HTTP endpoint.
New servers are `untrusted` unless `--trust trusted` is given.

## Importing from Claude Code, Codex and Warp

1. Run the preview (above) and show the user the rows. `new` can be added; `same` is already
   there; `conflict` is a different server under a name Relay already has and is never
   overwritten; `invalid` says why.
2. Ask which rows to bring in. Do not import everything because the user said "import my MCPs":
   show the list and let them pick, or get a plain "all of them".
3. `mcp_import --workspace . --add name1,name2` (or `--all`). They arrive untrusted.

Gallery installs in Warp live in Warp's database, not a file; those have to be added by hand.

## Project servers (.mcp.json)

A project's `.mcp.json` is listed as *not enabled* and never starts until the user enables it:
it comes from the repository, and enabling it runs its command. Show the command from `list`
and get a yes, then `mcp_config enable <server> --workspace .`. A change to the entry (a pull)
disables it again until it is re-enabled. `disable <server> --workspace .` turns it off.

## Trust

- **untrusted** (the default): each call puts an ask up naming the server; "Always allow" there
  trusts it.
- **trusted**: calls run with no ask.

`mcp_config trust <server> trusted|untrusted [--workspace .]`. Trusting means the model can use
every tool that server has, unasked. Only do it when the user says so for that server, and name
the server when you confirm it. Never trust a server to get past an ask you are waiting on.

## Turning off and removing

`disable --global <server>` keeps a global entry but stops loading it; `enable --global <server>`
brings it back; `remove <server>` deletes it. Removing is permanent. Confirm the name first.

## After a change

Changes apply from the pane's next configure or turn. A server that fails to start shows up as
a problem line in `list` and fails only its own calls, never the turn. If a server's tools do not
appear, run `list` and read its problem line before changing anything else.
