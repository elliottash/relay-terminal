<!-- relay:entry 20260924T135750Z-31 author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-subagent-tab-opens-on-raw-json-tool-results-unti.md

<!-- relay:entry 20260924T135753Z-mt author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session 58506d2c

<!-- relay:entry 20260924T135753Z-se author=agent kind=progress model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd pane_token=58506d2c-6f97-4190-b6b4-7d442f882f71 -->
Claimed (58506d2c) · working on it from a terminal pane

Fix the subagent_transcript snapshot: backend sends raw json.dumps tool results, surfaces dump them; make the snapshot carry per-tool labels and render folded tool rows like the live view.

<!-- relay:entry 20260924T135758Z-sc author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260924T141110Z-xe author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T141118Z-ha author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T141120Z-vf author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent moved this card · Executing → Needs verification · Snapshot now opens on folded tool rows; proved by TranscriptItemsTests, transcriptSnapshotOpensOnToolRows and the routing test; evidence in docs/qa_evidence/2026-09-24-m5fz-subagent-snapshot/ · evidence docs/qa_evidence/2026-09-24-m5fz-subagent-snapshot/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260924T141127Z-8c author=agent kind=progress model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260924T141436Z-b4 author=agent kind=progress model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260924T153039Z-yk author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent updated this card · appended to `## Try it`

<!-- relay:entry 20260924T153044Z-j6 author=agent kind=evidence model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
Try it staged and played mechanically: `docs/qa_evidence/2026-09-24-tryit-M5FZ/` — stage.sh (loopback-only scripted hosted model; one finished background subagent with two landed tool calls), ai-pass.sh exit 0 (subagent tab opens on the click, "2 tools", no raw JSON anywhere on screen), staging-notes.md. Whether the first paint looks right is the person's step (#M5FZ Try it). Also noted while staging: a custom provider at a loopback URL shows "no key yet" and cannot be selected in the model picker (customproviders.row reports has_stored_key False for the loopback key source) — filed as #HYBM-adjacent gap, inbox.
