# MCP server support

- **Status**: open
- **Component**: worker
- **Milestone**: post-mvp
- **Workstream**: agent
- **Acceptance evidence**: a configured MCP server's tools are available to pane agents
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "MCPs?"

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
