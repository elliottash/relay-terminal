<!-- relay:entry 20260920T123939Z-j1 author=agent kind=event model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
- ✦ agent created this card in Ready to start · issues/features/2026-09-20-uncap-turn-limits-by-default-loop-detection-reci.md

<!-- relay:entry 20260920T124008Z-fh author=agent kind=event model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
- ✦ agent updated this card · appended to `## Decisions`; tasks: 0/6 done

<!-- relay:entry 20260920T124020Z-84 author=agent kind=decision model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
Owner, 2026-09-20: "i think i really want it uncapped by default. people want to have long agent runs over night now in the age of astra." — and "i agree with all 3 layers you proposed" (deterministic loop detection, cadence recitation, LLM double-check on trigger).

<!-- relay:entry 20260920T124039Z-h8 author=agent kind=note model=kimi-k3 pane=02219b82 turn=798d406f5d60481a89cd76b2c0ac7b0c/6164c64d0dc44ed1922fd10bc3fd3a99 -->
Research (verified online 2026-09-20): OpenHands Stuck Detector — on by default, five deterministic patterns with thresholds (same action→same observation 4×, action→error 3×, monologue 3×, alternating cycle 6×, context-window errors), halts or nudges: https://docs.openhands.dev/sdk/guides/agent-stuck-detector. Gemini CLI LoopDetectionService — TOOL_CALL_LOOP_THRESHOLD=5, CONTENT_LOOP_THRESHOLD=10, plus LLM double-check every ~10 turns (confidence ≥0.9) with a productive-repetition whitelist prompt: packages/core/src/services/loopDetectionService.ts. Manus "Manipulate Attention Through Recitation" — todo.md recitation pushes objectives into recent attention; average task ≈50 tool calls: https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus. Anthropic "Effective context engineering" — context rot; compaction, note-taking, sub-agents: https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents. Relay already has the 8-step stale-todo nudge and end-of-turn completion check (max 2 reminders) to build on.
