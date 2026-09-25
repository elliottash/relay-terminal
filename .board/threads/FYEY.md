<!-- relay:entry 20260925T011953Z-q1 author=agent kind=event model=claude-opus-5-5 pane=64de364c turn=d7865438e5d344cabec7eded6da91d02/0975cf32d00b42b18c7df188b4539a1e -->
- ✦ agent created this card in Planned · .board/features/2026-09-24-stop-uncommitted-work-building-up-sessions-die-b.md

<!-- relay:entry 20260925T012014Z-1x author=agent kind=event model=claude-opus-5-5 pane=64de364c turn=d7865438e5d344cabec7eded6da91d02/0975cf32d00b42b18c7df188b4539a1e -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T012020Z-0c author=agent kind=event model=claude-opus-5-5 pane=64de364c turn=d7865438e5d344cabec7eded6da91d02/0975cf32d00b42b18c7df188b4539a1e -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T012553Z-f0 author=agent kind=note model=claude-opus-5-5 pane=64de364c turn=d7865438e5d344cabec7eded6da91d02/e2b7160370d943c1875e5b2fc03bc3a4 -->
One more cause, seen during this salvage: a subagent cannot be hard-stopped. The owner said "stop the agents"; the parent sent both a STOP message (`agent_message`, read "before its next step"). Subagent a1 still landed ceb4860f (#F8R7, 920 lines) 17 minutes later, after its first commit 9825875c had already been taken back in 221bfef7. Both commits were work the owner had said not to commit; both were taken back (221bfef7, a6013cf2). Add a step: the `agent` tool gets a real cancel that kills the subagent's process tree, and land.py refuses a commit from a session whose owning subagent was cancelled.
