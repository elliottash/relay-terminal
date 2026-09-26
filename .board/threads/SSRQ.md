<!-- relay:entry 20260919T234742Z-da author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Ready to start → Deferred · moved in the Switchboard

<!-- relay:entry 20260921T114351Z-tn author=agent kind=note model=kimi-k3 pane=switchboard turn=271ff3d5c65b4fa0a32b20f2b0e4898e/06b3d5e01bfb4df9827c77ae58b0ce14 -->
On #4NXH (guest board tools) the owner decided: "yes, i agree. and i am open to building SSRQ simultaneously." The guest board-tool surface is being built as its own thing (Relay-owned, pre-trusted, fixed tool set), but the owner is open to #SSRQ's generic MCP server support landing in parallel rather than waiting — worth un-deferring if a plan for it is wanted.

<!-- relay:entry 20260923T180814Z-25 author=owner kind=comment mode=discuss -->
has this been implemented now?

<!-- relay:entry 20260923T180853Z-bc author=agent kind=note model=kimi-k3 pane=switchboard turn=447de75741ac6655eedd96fb1bb082db/80ca7ee0096f42ada4a5dd1055734d3b -->
Owner asked "has this been implemented now?" — no. Repo check 2026-09-23: the only MCP in the codebase is the Relay-owned `relay_board` bridge for guest harnesses (#4NXH). No user-configured servers, trust levels, config import, or global/per-project configuration exist yet. Card stays deferred.

<!-- relay:entry 20260923T180900Z-19 author=agent kind=comment mode=discuss model=kimi-k3 turn=447de75741ac6655eedd96fb1bb082db/80ca7ee0096f42ada4a5dd1055734d3b -->
Let me check the codebase for MCP support.



No — checked the repo. The only MCP in the codebase is the Relay-owned `relay_board` bridge for guest agents (card #4NXH): a fixed, pre-trusted tool set wired into the Claude/Codex harnesses. None of #SSRQ's scope exists yet — no user-configured MCP servers, no trust levels, no config import from Claude Code/Codex/Warp, no global/per-project configuration.

**Not implemented.** Noted the answer on #SSRQ's thread; the card stays deferred. If you want to start it, the **Plan** button on the card will draft an implementation plan, or **Execute** hands it to a terminal pane.

<!-- relay:entry 20260925T201321Z-pn author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T201334Z-p3 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Deferred → Planning · changed in the Board

<!-- relay:entry 20260925T202441Z-th author=agent kind=event model=k3 pane=switchboard turn=9535d5d7d9d148737dd188e26e81cd73/825665f983e64a08a44192be1a10e44d -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T202454Z-sh author=agent kind=event model=k3 pane=switchboard turn=9535d5d7d9d148737dd188e26e81cd73/825665f983e64a08a44192be1a10e44d -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T202459Z-8v author=agent kind=comment mode=plan model=k3 turn=9535d5d7d9d148737dd188e26e81cd73/825665f983e64a08a44192be1a10e44d -->
The design doc read keeps truncating; let me read the section-9 area directly.



The reader returned the head again; let me pass an explicit line range.







The reader keeps returning the head; let me locate section 9 with search instead.





The reader keeps dropping my `from_line`. Trying once more with an explicit range, and checking the key agent/tool files in parallel.

The reader keeps returning the head; I'll locate section 9 via search instead.



The reader keeps truncating at line 129; I'll pull section 9's text via targeted searches instead.



I have the design-doc context I need (line 421: global Switchboard holds "global config (providers, intake …), memory, and possibly MCP server config"). Now let me look at the worker's tool plumbing.









I keep omitting `from_line` — passing it explicitly this time.

I keep omitting the `from_line` argument itself — passing it correctly now.



The reader is ignoring my line ranges; I'll work from targeted searches instead.

















The file reader keeps returning the head regardless of range; I'll work from `search_files`, which is reliable, to pin down the remaining facts.









I have what I need — the guest harnesses already carry MCP config (`relay_board`), `PluginTools` is the pattern for attaching an external tool group, and the design doc puts MCP server config in the global Switchboard. Writing the two sections now.







Plan written to #SSRQ. It builds a minimal stdio MCP client (`mcp_client.py`, no SDK), merges global + `.mcp.json` config with per-server trust hooked into `approvals.needed()`, attaches tools via the existing `PluginTools`/`ToolExecutor` pattern, folds user servers into the guest harnesses beside `relay_board`, and adds import-with-preview from Claude Code/Codex/Warp as a CLI step — GUI rows stay with #3KB7. One thing for you to confirm before Run: step 1 covers stdio servers only, with HTTP/SSE left for later.

<!-- relay:entry 20260925T202459Z-y9 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260926T000851Z-1g author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session 52e7f666

<!-- relay:entry 20260926T000851Z-ph author=owner kind=progress pane=switchboard pane_token=52e7f666-1bc2-4999-b7c2-37a733bcea9a -->
Claimed (52e7f666) · working on it from a terminal pane

<!-- relay:entry 20260926T001048Z-92 author=agent kind=progress model=claude-opus-5-5 pane=52e7f666 turn=e3e88db849674010ae393a9b2f595b05/607416e12948444dbf92e3f746b36d3e -->
Started executing. Two changes to the plan, and why:
1. **Guests go through `relay_board`, not their own `--mcp-config`** (plan step 6). A guest spawning user servers itself would bypass Relay's trust gate: with bypass permissions an untrusted server's tools would run with no ask. The bridge already dispatches through `agent._prepare/_execute` and already offers task-plugin groups, so MCP groups ride the same path and get the same trust ask.
2. **Project `.mcp.json` servers do not launch until enabled** in Relay's own config (`$XDG_CONFIG_HOME/relay/mcp-servers.json`, keyed by project path and pinned to a digest of the server's entry). This is the same rule task plugins follow. Otherwise, cloning a repository and opening a pane would run whatever command the repo names. Trust for a project server comes only from that enablement and defaults to untrusted.
Transports: stdio, plus streamable HTTP for `url` servers (plain POST + JSON/SSE response, static `headers`); legacy SSE and OAuth are refused with a readable error. Each server is one lazy tool group `mcp_<server>`, loaded with `load_tools` like a task plugin's group.

<!-- relay:entry 20260926T003911Z-ys author=agent kind=event model=claude-opus-5-5 pane=52e7f666 turn=e3e88db849674010ae393a9b2f595b05/607416e12948444dbf92e3f746b36d3e -->
- ✦ agent moved this card · Running → Needs verification, wrote `## Execution Summary`, `## Tests` · MCP client landed (97969b14, 1f2b4c17, evidence 9982be5e); every Done means line has a passing test, including two real-worker turns. · evidence docs/qa_evidence/2026-09-25-ssrq-mcp-support/ · implemented_by anthropic/claude-opus-5-5 via claude-code
