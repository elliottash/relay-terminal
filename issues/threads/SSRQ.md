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
