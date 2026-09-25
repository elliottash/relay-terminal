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

<!-- relay:entry 20260924T212302Z-dc author=agent kind=event model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by kimi/kimi-k3, session 857ae200

<!-- relay:entry 20260924T212302Z-kr author=agent kind=progress model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 pane_token=857ae200-ed0f-46b2-85d4-1c9065bb0a08 -->
Claimed (857ae200) · working on it from a terminal pane

Implementing the card's proposed fix: pane-like budget for general subagents, limit reported as limit with stop_reason, choose() warnings in the tool reply.

<!-- relay:entry 20260924T214525Z-pe author=agent kind=event model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260924T214538Z-n8 author=agent kind=event model=kimi-k3 pane=857ae200 turn=198f044065dd47079fe9509e50f12651/982d4c3c0b2e45aea46259f582724045 -->
- ✦ agent moved this card · Executing → Needs verification · Fixed in 946ec3b8: limit reports as limit with stop_reason and leading note, general budget follows #2CZP backstop, warnings reach tool replies. Unit tests added; verify build green. · implemented_by kimi/kimi-k3

<!-- relay:entry 20260924T234915Z-w3 author=owner kind=comment mode=discuss -->
i dont want the 12 step limit any more, give subagents the same limit as main agents

<!-- relay:entry 20260924T235009Z-65 author=agent kind=event model=glm-5.3 pane=switchboard turn=2e2617b4c654e4078aff9af15bcebb0a/9e7195357c3c4f998089d7b16af1c412 -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260924T235015Z-m6 author=agent kind=decision model=glm-5.3 pane=switchboard turn=2e2617b4c654e4078aff9af15bcebb0a/9e7195357c3c4f998089d7b16af1c412 -->
Owner: "i dont want the 12 step limit any more, give subagents the same limit as main agents". `general` already got 500 in 946ec3b8; this decision also takes the 12 off the `signal` definition (agents_defs.py:126) — a one-line follow-up for a Run pane, since Discuss cannot write code.

<!-- relay:entry 20260924T235023Z-04 author=agent kind=comment mode=discuss model=glm-5.3 turn=2e2617b4c654e4078aff9af15bcebb0a/9e7195357c3c4f998089d7b16af1c412 -->
The landed fix already does most of this: `general` subagents got the pane's 500-step fuse in 946ec3b8. The one place a 12-step limit survives is the `signal` definition (`agents_defs.py:126`) — Relay's own fault-pickup runs. Your decision removes that too; recording it on the card.



Recorded the decision on #VTJR (in `## Decisions` and the thread).

Where it stands:

- **Already done for `general`:** 946ec3b8 raised the default from 12 to 500 — the same backstop fuse a main agent gets (#2CZP). Agent-spawned subagents no longer have a 12-step limit.
- **One 12 left:** the `signal` definition (Relay's own unattended fault-pickup runs) still carries an explicit 12 at `agents_defs.py:126`. Your decision removes that too — a one-line change plus the test that asserts 12.

I can't write code from Discuss: press **Run** on this card and a terminal pane lands the signal change. Verification of the rest (limit reported as `limit`, model warnings in replies) is still pending in needs-verification.

<!-- relay:entry 20260924T235854Z-te author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T235945Z-xn author=agent kind=event model=glm-5.3 pane=switchboard turn=2e2617b4c654e4078aff9af15bcebb0a/92c40504d9774df2b63a0150949a1e89 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T235956Z-tt author=agent kind=event model=glm-5.3 pane=switchboard turn=2e2617b4c654e4078aff9af15bcebb0a/92c40504d9774df2b63a0150949a1e89 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T000002Z-3f author=agent kind=comment mode=plan model=glm-5.3 turn=2e2617b4c654e4078aff9af15bcebb0a/92c40504d9774df2b63a0150949a1e89 -->
The remaining work is one line of code plus its test. Writing the revised `## Done means` and the `## Plan`.



Plan written to #VTJR. It is the owner's decision carried through: `signal`'s trailing `12` in `agents_defs.py:126` becomes `MAX_STEPS` (one line), the budget test asserts the builtin `signal` definition at 500, and any doc or test still promising signal a small budget is fixed. Nothing needs deciding — a signal-specific cost cap was considered and left out as a separate matter — so **Run** can take it straight to land.py.

<!-- relay:entry 20260925T000235Z-d1 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · Needs verification → Executing, session aa03e45a

<!-- relay:entry 20260925T000235Z-s9 author=owner kind=progress pane=switchboard pane_token=aa03e45a-c6a4-4332-a7a7-acee4cca3b28 -->
Claimed (aa03e45a) · working on it from a terminal pane

<!-- relay:entry 20260925T000319Z-t7 author=agent kind=event model=claude-opus-5-5 pane=aa03e45a turn=db8d13771cd644a28eac5ada2a8485f2/706a00f3bffa49ea874983f67abfc7ef -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["2CZP", "9HS0"], "githu… → {"plans": [], "commits": ["946ec3b8", "b64b01c6", "2c7c7774"], "evidence": [], "…; appended to `## Tests`; replaced `## Execution Summary`

<!-- relay:entry 20260925T000320Z-h5 author=agent kind=event model=claude-opus-5-5 pane=aa03e45a turn=db8d13771cd644a28eac5ada2a8485f2/706a00f3bffa49ea874983f67abfc7ef -->
- ✦ agent moved this card · Executing → Needs verification · signal definition now at the 500-step backstop (2c7c7774); 203 targeted tests pass · evidence tests/test_subagents.py · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T000323Z-8z author=agent kind=progress model=claude-opus-5-5 pane=aa03e45a turn=db8d13771cd644a28eac5ada2a8485f2/706a00f3bffa49ea874983f67abfc7ef -->
Landed 2c7c7774: the `signal` definition now uses `MAX_STEPS` (500), like `general`, and a new test checks that every builtin definition is at 500. 203 tests pass across test_subagents, test_signals, test_signal_threads and test_agent. The card is in needs-verification. The live `turn_start` probe is left for the verifier.
