<!-- relay:entry 20260920T123939Z-j1 author=agent kind=event model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
- ✦ agent created this card in Ready to start · issues/features/2026-09-20-uncap-turn-limits-by-default-loop-detection-reci.md

<!-- relay:entry 20260920T124008Z-fh author=agent kind=event model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
- ✦ agent updated this card · appended to `## Decisions`; tasks: 0/6 done

<!-- relay:entry 20260920T124020Z-84 author=agent kind=decision model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
Owner, 2026-09-20: "i think i really want it uncapped by default. people want to have long agent runs over night now in the age of astra." — and "i agree with all 3 layers you proposed" (deterministic loop detection, cadence recitation, LLM double-check on trigger).

<!-- relay:entry 20260920T124039Z-h8 author=agent kind=note model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
Research (verified online 2026-09-20): OpenHands Stuck Detector — on by default, five deterministic patterns with thresholds (same action→same observation 4×, action→error 3×, monologue 3×, alternating cycle 6×, context-window errors), halts or nudges: https://docs.openhands.dev/sdk/guides/agent-stuck-detector. Gemini CLI LoopDetectionService — TOOL_CALL_LOOP_THRESHOLD=5, CONTENT_LOOP_THRESHOLD=10, plus LLM double-check every ~10 turns (confidence ≥0.9) with a productive-repetition whitelist prompt: packages/core/src/services/loopDetectionService.ts. Manus "Manipulate Attention Through Recitation" — todo.md recitation pushes objectives into recent attention; average task ≈50 tool calls: https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus. Anthropic "Effective context engineering" — context rot; compaction, note-taking, sub-agents: https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents. Relay already has the 8-step stale-todo nudge and end-of-turn completion check (max 2 reminders) to build on.

<!-- relay:entry 20260920T134548Z-wg author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T135643Z-qf author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=144413a1bae94e1aae8d0b83e112a959/008a90e5f287442ba917029da54731e7 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T135654Z-zx author=agent kind=question model=deepseek/deepseek-v4.1-flash pane=switchboard turn=144413a1bae94e1aae8d0b83e112a959/008a90e5f287442ba917029da54731e7 -->
Plan written; two choices it assumes, both cheap to change now:\n1. Which model runs the trigger-only double-check? Recommended: a **new `loop_check` role** on the Lite tier with its own row in the roles modal (separate from `audit`, which answers a different prompt). The alternative is reusing the existing `audit` role — less code, one row standing for two jobs.\n2. How does the escalation report? Recommended: keep `stop_reason: \"limit\"` and add `limit.which: \"loop\"`, so the Continue path and the limit line in the GUI keep working unchanged (`src/Pane.h:897`, `RequestLedger.h:127`) — the alternative, a new `stop_reason` value, needs GUI changes in two files.

<!-- relay:entry 20260920T135703Z-kk author=agent kind=comment mode=plan model=deepseek/deepseek-v4.1-flash turn=144413a1bae94e1aae8d0b83e112a959/008a90e5f287442ba917029da54731e7 -->
**Done:** #2CZP now has a `## Plan` — defaults move to the clamp maxima (500/2000) as a backstop fuse in `agent.py:56-57` and `src/Pane.h:4023-4024`, with the three layers built on the existing reminder machinery: a new pure `backend/relay_core/loopdetect.py` wired into the loop at `agent.py:1321-1445`, cadence recitation beside the current notes at `:1342-1388`, and a trigger-only double-check shaped like `_start_audit` (`:2160-2194`), plus docs and targeted unittest coverage.

**Need:** your word on two assumptions (also on the card as a question): a new `loop_check` Lite-tier role rather than reusing `audit`, and reporting the escalation as `stop_reason: "limit"` with `limit.which: "loop"` rather than a new stop reason. The plan's Orchestration block also allows a docs subagent and a test-file subagent in parallel with the main diff.

<!-- relay:entry 20260920T141649Z-5z author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T141649Z-nn author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Ready to start → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T141650Z-0r author=owner kind=progress pane=switchboard pane_token=6a2bc14e-5eaa-4eb2-9b7b-ac7106d6f5a4 -->
Executing (6a2bc14e) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T170500Z-ld author=agent kind=event model=anthropic/claude-opus-5 pane=terminal -->
- ✦ agent updated this card · tasks: 6/6 done; added `## QA checklist`; status: Executing → Needs verification; links.commits: `2d2a7103`; links.evidence: `docs/qa_evidence/2026-09-20-uncap-turn-limits`

<!-- relay:entry 20260920T170501Z-qz author=agent kind=decision model=anthropic/claude-opus-5 pane=terminal -->
The plan's two open questions were answered as the plan recommended, since it was handed to a terminal pane to execute as written: a **new `loop_check` role** on the Lite tier (its own row in Options › Models › Advanced, beside Request audit — the two answer different prompts), and the escalation reports as **`stop_reason: "limit"` with `limit.which: "loop"`** rather than a new stop reason, so the pane's Continue path is untouched. Both are cheap to reverse if the owner wants otherwise: the role is four table entries in `roles.py` plus its mirror in `src/Pane.h`, and the stop reason is one string in `_stop_at_limit` plus the branch in `RequestLedgerModel::limitLine`.

<!-- relay:entry 20260920T170502Z-9t author=agent kind=note model=anthropic/claude-opus-5 pane=terminal -->
Three things the detector's own test pass turned up in `loopdetect.py`, all fixed before landing rather than listed: a tool result that is circular or nested past the interpreter's recursion limit raised `RecursionError` inside the hash (now bounded by `SCRUB_DEPTH`, hashing as its repr past that); the longest `error` nudge reached 431 characters against a promise of one short paragraph (details are now quoted at `DETAIL_CAP`, longest is 392); and the module docstring claimed "re-running a build after an edit" could never fire, which is true of `repeat` and `error` but not of `cycle` — a *byte-identical* edit followed by the *same* failure six calls running does fire, and deliberately so. The docstring now says that instead of over-promising.

<!-- relay:entry 20260920T170503Z-b4 author=agent kind=comment model=anthropic/claude-opus-5 pane=terminal -->
**Done, landed in `2d2a7103`.** Turn limits are uncapped by default — `DEFAULT_MAX_STEPS` 500 and `DEFAULT_MAX_TOOL_CALLS` 2000, `validate_turn_options`' own clamp maxima, with both Options › Security rows reworded as a backstop fuse. What ends a turn going nowhere is the three layers you agreed to: a new pure `backend/relay_core/loopdetect.py` (repeat 4×, error 3×, cycle over 6, monologue 3×, all over *consecutive* observations — which is what makes the productive-repetition whitelist hold without a list of exceptions), a cadence recitation every 25 steps or 50 tool calls built from the ledger and todos at no model cost, and a trigger-only double-check on the new `loop_check` Lite role that can only ever forgive, never block. Two ignored nudges end the turn through the existing limit path, so Continue still works.

Proved against the real worker and the real app, not just unit tests: `configure` with nothing stored reports 500/2000; a turn of 30 distinct reads recites once at step 25 and finishes with no stop; a turn repeating one failing call shows `↻ read_file 3 times with the same result · asked to change approach (1/2)`, then `(2/2)`, then `· stopping this turn`, and ends on `‖ Stopped: repeating itself (…)` with `▸ Continue`. Evidence, both drivers and the logs are in `docs/qa_evidence/2026-09-20-uncap-turn-limits/`; targeted tests are 61 + 40 + 58 + 43 unittest and the `requests`/`panestate` ctest cases.

**Ready for verification.** The QA checklist is on the card; the one thing worth a human eye is false positives on a genuinely long run of your own work — the nudge comes first and only the third trigger stops anything, but the thresholds are the judgement call here.
