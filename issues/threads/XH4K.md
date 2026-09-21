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
