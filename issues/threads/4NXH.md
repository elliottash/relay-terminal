<!-- relay:entry 20260920T125058Z-kx author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=9f3c40626beb405aa9f5fccedd3f5441/ac7af8a634384b0d866c3d1cce0c26a9 -->
- ✦ agent created this card in Discussing · issues/changes/2026-09-20-a-guest-session-codex-claude-code-cannot-use-the.md

<!-- relay:entry 20260920T125110Z-zh author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=9f3c40626beb405aa9f5fccedd3f5441/ac7af8a634384b0d866c3d1cce0c26a9 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Findings`

<!-- relay:entry 20260920T125123Z-c0 author=agent kind=question model=deepseek/deepseek-v4.1-flash pane=switchboard turn=9f3c40626beb405aa9f5fccedd3f5441/ac7af8a634384b0d866c3d1cce0c26a9 -->
1. **How should a guest reach the board — give it the tools, or keep it tool-less and let Relay write?**
   (a) Serve Relay's board tools to the guest harness (the harness configures the guest with an MCP surface; the same `board_tools.py` guardrails and autonomy apply), so a verifier can call `board_read`, `board_comment` and `board_move_card` itself — the approach that matches how the briefs already read.
   (b) Keep guests without tools: rewrite the Execute/Verify briefs so a guest never needs a board call (it reports in its reply, and Relay or the Switchboard agent does the writes).
   *Recommendation: (a), read-only plus the writes must have (`board_read`, `board_comment`, `board_move_card`, `board_update_card`) — it is the only route that lets a guest verifier close its own card, and it reuses the guardrails that already exist. (b) is smaller but leaves every guest turn's board work to be hand-relayed.*

2. **Should this land inside #SSRQ (MCP server support, deferred) or as its own guest-only surface?**
   *Recommendation: its own surface — a guest is a Relay-owned harness, not a user-configured MCP server, and #SSRQ is about trusting third-party servers. Say so and I move this card out of `discussing`.*
