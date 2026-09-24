<!-- relay:entry 20260924T185051Z-wm author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/2e3b0f70603246648adff31686561ad8 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-a-subagent-that-hits-its-12-step-limit-is-report.md

<!-- relay:entry 20260924T185122Z-jj author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/2e3b0f70603246648adff31686561ad8 -->
- ✦ agent updated this card · replaced `## Discussion points`

<!-- relay:entry 20260924T185135Z-n0 author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/2e3b0f70603246648adff31686561ad8 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["probe"], "human": "optional"…; replaced `## Done means`

<!-- relay:entry 20260924T190709Z-4t author=agent kind=evidence model=gpt-6-astra pane=a06c9360 turn=66f5f9b23b56456998d23d00ca79805b/7eaa194cda7243b8893c8cdf6a1444bc -->
Reproduced during #XCXD implementation on 2026-09-24. User explicitly requested GPT-6-Sol subagents. agent(model='gpt-6-sol') returned success for a1/a2/a3, and agent_set_model(id='all', model='gpt-6-sol') returned changed:3, but all saved child thread records under session 66f5f9b23b56456998d23d00ca79805b.threads identify model glm-5.3, models ['glm-5.3']. Early runs hit the 12-step limit and agent_wait reported done with progress text/no final report. Requested children stop; a1 confirms no edits. This blocks honoring the owner's requested model. Factory.choose resolves gpt-6-sol via the OpenAI preset, then silently returns its base when that preset has no usable key; it does not resolve the available Codex guest model from that name. Warnings are absent from agent and agent_set_model tool replies.

<!-- relay:entry 20260924T190709Z-x0 author=agent kind=event model=gpt-6-astra pane=a06c9360 turn=66f5f9b23b56456998d23d00ca79805b/7eaa194cda7243b8893c8cdf6a1444bc -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
