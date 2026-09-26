<!-- relay:entry 20260920T160409Z-ex author=claude-code kind=progress -->
### Claude Code · 2026-09-20 16:04
filed from the owner's request in a Claude Code session and claimed; no earlier card covered the usage figure in the picker or the exhausted skip (#DC4J is /swap, #YJG7 the backend's 429-as-transient). Starting on src/ModelCatalog.*, src/ModelPicker.cpp, the Pane's usage_limits handling and Options › Models' row detail.

<!-- relay:entry 20260920T160410Z-ey author=claude-code kind=event -->
- ✦ claude-code moved this card · Inbox → Needs verification · usage limits reach the picker live; an exhausted subscription is greyed and skipped in the priority until it resets · evidence docs/qa_evidence/2026-09-20-usage-limits-picker/

<!-- relay:entry 20260920T213305Z-de author=agent kind=note model=glm-5.3 pane=bbc44187 turn=6dbff68168874e95b1b29753e78727f6/6f0c8c758af946e984549cce3d7a980d -->
Owner report: exhausted z.ai subscription still showed normally in the picker. Checked from a Relay pane: the implementation (e8220208, 2026-09-20 12:07) is in HEAD and `build/relay` was relinked 17:27, but the owner's running Relay process was started 2026-09-19 15:54 — the running instance predates the feature, so nothing of this card was live. Also noted: z.ai has no quota endpoint wired (no `usage_limits` source for GLM), so even after a restart the exhausted marking for z.ai only appears through the 429 path — a turn that gets a 429 through the transport's retries and then fails over or dies marks it for 30 min (QA checklist item 4). Needs a fresh verification run on a restarted Relay.

<!-- relay:entry 20260920T220737Z-r5 author=agent kind=note model=z-ai/glm-5.3-flashx pane=bbc44187 turn=6dbff68168874e95b1b29753e78727f6/98f06163f88042e689884cbbd05c3637 -->
Correction to my 21:33 note: z.ai does have a quota endpoint. The owner runs a usage tracker (`/home/elliott/data/usage_tracker/check_usage.py`, cron every 4h) that polls `GET https://api.z.ai/api/monitor/usage/quota/limit` (Bearer ZAI_API_KEY) — response `data.limits[]` with `unit == 6` marking the weekly window, each entry carrying `percentage` and `nextResetTime`. Kimi's equivalent: `GET https://api.kimi.com/coding/v1/usages` (Bearer KIMI_API_KEY) → `usage.limit`/`usage.remaining`/`usage.resetTime`. Owner approved wiring these into the worker's usage-limits path so GLM/Kimi presets show live figures in the picker and get marked exhausted with the vendor's own reset time, without waiting for a 429.

<!-- relay:entry 20260920T220747Z-41 author=agent kind=event model=z-ai/glm-5.3-flashx pane=bbc44187 turn=6dbff68168874e95b1b29753e78727f6/98f06163f88042e689884cbbd05c3637 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260920T220812Z-7z author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T221301Z-2w author=agent kind=event model=kimi-k3 pane=switchboard turn=12ba9f2b437348d6842f70e70a87887a/5049e3cd06494c4d819cb2e574bef3a6 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T221313Z-aw author=agent kind=comment mode=plan model=kimi-k3 turn=12ba9f2b437348d6842f70e70a87887a/5049e3cd06494c4d819cb2e574bef3a6 -->
Now let me look at the protocol's `usage_limits` event spec, the ARCHITECTURE paragraph to correct, and how the worker's existing background fetch pattern works (`openrouter_catalog`).













One more check — how the pane decides a provider has "no quota endpoint" for the 30-min 429 heuristic:

I have the full picture. Writing the plan now.

**Done:** the plan on #XH4K is now an execution-ready plan for wiring z.ai and Kimi quota endpoints into the worker's `usage_limits` path — a new `backend/relay_core/provider_limits.py` on the `openrouter_catalog` background-fetch pattern, `worker.py` wiring into `emit_presets`, comment/doc corrections (ARCHITECTURE.md:1883, protocol 29.3), and a new `tests/test_provider_limits.py`; the GUI needs no functional change.

One thing the executor must do first: read `/home/elliott/data/usage_tracker/check_usage.py` from the terminal (it's outside this workspace) to confirm the Kimi host — the thread note says `api.kimi.com` but the preset's base URL is `api.kimi.ai` — and the timestamp units. That check, and whether z.ai's endpoint serves the payg `glm` key at all, are flagged in the plan's Risks.

<!-- relay:entry 20260923T181718Z-cd author=codex kind=progress -->
### Codex · 2026-09-23 18:17
Owner asked me to claim #XH4K and deliver the Z.AI/Kimi quota integration. Taking over from needs-verification and implementing its existing plan.

<!-- relay:entry 20260923T181803Z-v3 author=owner kind=comment mode=discuss -->
refresh this and update the plan

<!-- relay:entry 20260923T182237Z-9g author=agent kind=event model=gpt-6-sol pane=b46708c9 turn=bf07ea53fe1e47b383aeb5e7025ce0da/dc8fae0bc52242ec9bd87d8687cb21ba -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260923T182449Z-sz author=agent kind=event model=kimi-k3 pane=switchboard turn=e5bdaeb6520bedac957e60f0d4a328d8/c85dc1c43be74c7bb90bd11b057c2676 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T182504Z-t1 author=agent kind=comment mode=discuss model=kimi-k3 turn=e5bdaeb6520bedac957e60f0d4a328d8/c85dc1c43be74c7bb90bd11b057c2676 -->
I'll check the current state of the code against the plan, then refresh it.



No `provider_limits.py` yet — Codex's claim hasn't produced code. Let me verify the plan's other references are still accurate.

Line numbers have drifted and the "no quota endpoint" wording is gone. Checking the current GUI and docs wording:



The GUI code seems to have moved out of `Pane.h`. Searching more broadly:





`provider_limits.py` now exists — the backend work has been started. Let me read it and check the worker wiring and tests:











There's a `PaneUsage.cpp` — likely where the limits handling moved:

















One more check on whether the 429 heuristic survived the GUI refactor anywhere:







The card changed under me — re-reading before reapplying:

Codex added a `## Done means` section in the meantime. Reapplying my Plan refresh on the new hash:

**Done:** refreshed the `## Plan` on #XH4K against the current tree. What it now says:

- **Already landed:** `backend/relay_core/provider_limits.py` (z.ai + Kimi polling, 15-min cadence), the `worker.py` wiring (`start`, listener → re-push `presets`, `limits` on built-in rows), and both doc updates (ARCHITECTURE.md:2416, protocol 29.3). Deviations from the old plan are recorded: payg `glm` dropped, Kimi host is `api.kimi.com`, unchanged figures re-emit on purpose.
- **Still to do:** (1) the planned `tests/test_provider_limits.py` was never written; (2) reconcile the GUI — the exhausted/grey logic survives in `ModelCatalog`/`ModelPicker`/`ModelRows`, but `noteLimits` and the 429 `markExhausted` heuristic are no longer findable in `src/` while ARCHITECTURE.md still describes them, so QA item 4 may be broken or the docs stale; (3) live verification on a restarted Relay — the QA checklist is still fully unchecked.

Writing the tests and code is **Run** — hand the card to a terminal pane when you're ready.

<!-- relay:entry 20260923T182532Z-c5 author=agent kind=event model=gpt-6-sol pane=b46708c9 turn=bf07ea53fe1e47b383aeb5e7025ce0da/dc8fae0bc52242ec9bd87d8687cb21ba -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["e8220208"], "evidence": ["docs/qa_evidence/2026-09-20… → {"plans": [], "commits": ["e8220208", "38f5e4ceb5c6244333aba61c015b3bcf30686005"…; appended to `## Execution Summary`

<!-- relay:entry 20260923T182538Z-7s author=agent kind=event model=gpt-6-sol pane=b46708c9 turn=bf07ea53fe1e47b383aeb5e7025ce0da/dc8fae0bc52242ec9bd87d8687cb21ba -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T182542Z-cv author=agent kind=event model=gpt-6-sol pane=b46708c9 turn=bf07ea53fe1e47b383aeb5e7025ce0da/dc8fae0bc52242ec9bd87d8687cb21ba -->
- ✦ agent moved this card · Executing → Needs verification · Z.AI and Kimi quota polling landed; offline tests, exact-tree build, and live read-only probes passed. Restarted UI and exhaustion behavior await independent verification. · evidence docs/qa_evidence/2026-09-23-xh4k/ · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260923T182628Z-rd author=agent kind=event model=gpt-6-sol pane=b46708c9 turn=bf07ea53fe1e47b383aeb5e7025ce0da/dc8fae0bc52242ec9bd87d8687cb21ba -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T204829Z-zk author=agent kind=event mention=QK2Q model=claude-opus-5-5 pane=7f908958 turn=eb540dbc335b48608bf140f752a963ce/97053e75276c4d7da4511160d884309d -->
mentioned in #QK2Q · 2026-09-25 · agent

<!-- relay:entry 20260925T222630Z-2x author=agent kind=event mention=EQH0 model=claude-opus-5-5 pane=cd39c532 turn=eb540dbc335b48608bf140f752a963ce/2533e01880b34962874cd7d74c7d59de -->
mentioned in #EQH0 · 2026-09-25 · agent

<!-- relay:entry 20260925T224819Z-74 author=agent kind=event mention=YC0T model=claude-opus-5-5 pane=cd39c532 turn=eb540dbc335b48608bf140f752a963ce/5b5c3e27cc2d46adaf367f65e81a488f -->
mentioned in #YC0T · 2026-09-25 · agent
