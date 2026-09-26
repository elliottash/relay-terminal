<!-- relay:entry 20260925T165538Z-wd author=agent kind=event model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-prompt-can-t-reach-an-agent-waiting-on-backgroun.md

<!-- relay:entry 20260925T165544Z-88 author=agent kind=event model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, session d53ee6b1

<!-- relay:entry 20260925T165544Z-wc author=agent kind=progress model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 pane_token=d53ee6b1-5068-4c69-bc11-e9cc04fb441e -->
Claimed (d53ee6b1) · working on it from a terminal pane

Reproducing the flow in code: steers land at step boundaries only, and a turn parked in agent_wait has none. Fix is worker wake + pane steer-on-submit during background waits, with tests.

<!-- relay:entry 20260925T165600Z-sn author=agent kind=event model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "criteria…; appended to `## Done means`; replaced `## Plan`

<!-- relay:entry 20260925T170650Z-ag author=agent kind=event model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent updated this card · appended to `## Execution Summary`; replaced `## Tests`

<!-- relay:entry 20260925T170653Z-7d author=agent kind=event model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent moved this card · Executing → Needs verification · Fix landed as 218d8fea with worker + pane tests green; separate session to verify. · evidence docs/qa_evidence/2026-09-27-t4vk-steer-wakes-agent-wait/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T170722Z-29 author=agent kind=progress model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T170855Z-y5 author=agent kind=event model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
- ✦ agent updated this card · replaced `## Try it`

<!-- relay:entry 20260925T170903Z-fs author=agent kind=evidence model=glm-5.3 pane=d53ee6b1 turn=0b9295b56ae5446fbc563a5065aa88f6/d160278a6c154aee9a4631a0438809d7 -->
Try it staged by me (no verifying-session staging existed; backend timeline, not the app window — a real agent pane needs a model key, which isolated staging keeps away from the real keyring; see staging-notes.md). Ran `docs/qa_evidence/2026-09-25-tryit-T4VK/stage.sh` from the repo root: the steer submitted at t+0.56 s ended the `agent_wait` at t+0.58 s with `stopped_for_user_message=True`, `timed_out=False`, subagent still `running`; the model's next request ended with the steered text as its last user message; one turn started in total. Capture: `docs/qa_evidence/2026-09-25-tryit-T4VK/01-timeline.txt`. The pane's one-Enter decision itself is pinned by `tests/queuesubmit_test.cpp`, not by this timeline.
