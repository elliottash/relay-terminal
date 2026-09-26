<!-- relay:entry 20260926T125125Z-8r author=agent kind=event model=claude-opus-5-5 pane=b65a84fc turn=45c2ee31a8d34d5184a33977aa649f5f/e338117fd61549edb407aca486980abf -->
- ✦ agent created this card in Planning · .board/features/2026-09-26-mcp-oauth-sign-in-to-remote-mcp-servers-github-l.md

<!-- relay:entry 20260926T125127Z-7n author=agent kind=event model=claude-opus-5-5 pane=b65a84fc turn=45c2ee31a8d34d5184a33977aa649f5f/e338117fd61549edb407aca486980abf -->
- ✦ agent claimed this card · assignee agent, Planning → Running, implemented_by anthropic/claude-opus-5-5 via claude-code, session b65a84fc

<!-- relay:entry 20260926T125127Z-7o author=agent kind=progress model=claude-opus-5-5 pane=b65a84fc turn=45c2ee31a8d34d5184a33977aa649f5f/e338117fd61549edb407aca486980abf pane_token=b65a84fc-848b-48e8-8d87-d77e2cf7423d -->
Claimed (b65a84fc) · working on it from a terminal pane

Planning MCP OAuth on top of the #SSRQ client; plan goes on the card before any code.

<!-- relay:entry 20260926T125240Z-yj author=agent kind=event model=claude-opus-5-5 pane=b65a84fc turn=45c2ee31a8d34d5184a33977aa649f5f/e338117fd61549edb407aca486980abf -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["person"], "human": "required…; replaced `## Done means`

<!-- relay:entry 20260926T125306Z-89 author=agent kind=event model=claude-opus-5-5 pane=b65a84fc turn=45c2ee31a8d34d5184a33977aa649f5f/e338117fd61549edb407aca486980abf -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260926T125310Z-fy author=agent kind=question model=claude-opus-5-5 pane=b65a84fc turn=45c2ee31a8d34d5184a33977aa649f5f/e338117fd61549edb407aca486980abf -->
Before the UI step (steps 1–3 can start without an answer):
1. Some servers don't support dynamic client registration, GitHub's hosted MCP server among them. Should Relay register its own OAuth apps with GitHub, Slack and similar providers, or should users supply their own `oauth.client_id` for now? *Recommendation: user-supplied `client_id` now. Relay-owned apps once there's a release and a domain to register them under.*
2. Should Relay publish a Client ID Metadata Document at a stable HTTPS URL (e.g. `https://relay…/oauth/client.json`)? It's the spec's preferred registration method. *Recommendation: yes, once a domain exists. Until then, dynamic registration only.*
3. Should legacy SSE-only MCP servers stay refused? *Recommendation: yes. Most hosted servers now offer streamable HTTP.*
