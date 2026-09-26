<!-- relay:entry 20260925T204828Z-cg author=agent kind=event model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-provider-failures-say-which-limit-or-credential.md

<!-- relay:entry 20260925T204831Z-d3 author=agent kind=event model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d -->
- ✦ agent claimed this card · assignee agent, Inbox → Running, implemented_by anthropic/claude-opus-5-5 via claude-code, session 7f908958

<!-- relay:entry 20260925T204831Z-d4 author=agent kind=progress model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d pane_token=7f908958-18c6-4f6e-8f75-cfbbc71d3e50 -->
Claimed (7f908958) · working on it from a terminal pane

Classify provider refusals in provider.py (z.ai 1308/1310/…, Kimi token expiry), re-read rotated keys on 401, surface reason + helper offer; then investigate routing miss.

<!-- relay:entry 20260925T210401Z-s6 author=agent kind=event model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["probe"], "human": "optional"…; replaced `## Done means`

<!-- relay:entry 20260925T210528Z-kq author=agent kind=event model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d -->
- ✦ agent updated this card · appended to `## Execution Summary`; replaced `## Tests`

<!-- relay:entry 20260925T210531Z-0h author=agent kind=event model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d -->
- ✦ agent moved this card · Running → Needs verification · Landed 4947ff81 with tests and a live Z.AI probe; the helper-link click needs a live GUI check by a verifier. · evidence docs/qa_evidence/2026-09-25-qk2q/ · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T211901Z-c3 author=agent kind=event mention=9R2V model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/047290531fcc4e75a109a44aa2cf6a4b -->
mentioned in #9R2V · 2026-09-25 · agent

<!-- relay:entry 20260925T212113Z-4k author=agent kind=decision model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/523443f0d7bb4d9386eaf115a8db2696 -->
Owner, 2026-09-25: "can we change failovers to do default model picking including the weighted radnomization if enabled". Every failover chain (ordinary, plan-list, side call) is ordered by the same `ordered_candidates(choose=True)` draw the new-pane default and the quota failover use: ranks stay authoritative, tied ranks are drawn by remaining quota per hour, spent entries are excluded.

<!-- relay:entry 20260925T212249Z-hf author=agent kind=evidence model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/523443f0d7bb4d9386eaf115a8db2696 -->
Landed efa75963: ordinary, plan-list and side-call failover chains are ordered by `ordered_candidates(choose=True)` (surfaces `failover:<tier>`, `failover:high`, `side_failover:<tier>`). New test `test_a_tied_rank_is_drawn_by_remaining_quota_like_a_new_panes_default`; tests/test_provider_errors.py 17 passed; test_failover + test_roles + test_provider 214 passed, 1 failing identically at HEAD (twin-once). Note: the older `fallbacks` option carries no ranks, so only `tiers` lists can tie.

<!-- relay:entry 20260925T224819Z-ef author=agent kind=event mention=YC0T model=claude-opus-5-5 pane=cd39c532 turn=eb540dbc335b48608bf140f752a963ce/5b5c3e27cc2d46adaf367f65e81a488f -->
mentioned in #YC0T · 2026-09-25 · agent

<!-- relay:entry 20260925T235213Z-a7 author=agent kind=event mention=P004 model=k3 pane=1eea0ad8 turn=f1140809e6f14769866887282d5dc791/0c4d631200dd4f54be9188178d130369 -->
mentioned in #P004 · 2026-09-25 · agent
