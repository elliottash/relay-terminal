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

<!-- relay:entry 20260921T111652Z-cb author=codex kind=comment -->
### Codex · 2026-09-21 11:16
Owner report:
> potential bug, codex just told me it had no board_ tools
>
> session 85d4ba99d2a54dcdb8f6332ff93b4925
>
> "The old behavior is still present. Card-ID copying also shares the label-copy
> helper, so I’ll separate those paths to preserve #ID copies while labels copy as
> label:name. I’ll use the policy’s file-edit fallback for card updates because this
> session has no board_* tools."

Confirmed against the saved Relay session: `guest: codex`, `model: gpt-6-astra`, guest session `01a0c3ac-2e03-77f2-bc32-f1828a6f5159`. `HarnessProvider.complete` in `backend/relay_core/guest_harness_provider.py` receives a `tools` argument but calls the harness with only the prompt, attachments, event callback and cancellation. Relay board tools are not forwarded. This is the existing guest-tool limitation tracked here; the fallback in `issues/POLICY.md` is intentional (commit `7bd892f2`, #R9G7). No runtime code changed and no takeover of this discussing card. A board-tool bridge remains unimplemented.
