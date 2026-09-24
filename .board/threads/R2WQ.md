<!-- relay:entry 20260919T203510Z-tm author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Ready to start → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T203510Z-tw author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T203521Z-ax author=agent kind=progress model=glm-5.3 pane=8a3262cb turn=f3c4a3de6a60443cb6e8c5c0d285b4bc/9e1d773cd0fb4fa19917b83a1489489e -->
Starting execution: reading Pane::wrapped, WordWrap, FoldLayer and the cores first, then implementing the five tasks in the fix plan in order (anchor → logical lines → replacement folds → shared break rule → tests).

<!-- relay:entry 20260919T224940Z-vm author=agent kind=event model=glm-5.3 pane=ca0785f6 turn=f3c4a3de6a60443cb6e8c5c0d285b4bc/871f2c0269504297bcd851dee4775a1a -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-19-relays-ow… → {"plans": [], "commits": ["121fd9a1", "b6521ebc"], "evidence": ["docs/qa_evidenc…

<!-- relay:entry 20260919T224956Z-tb author=agent kind=progress model=glm-5.3 pane=ca0785f6 turn=f3c4a3de6a60443cb6e8c5c0d285b4bc/871f2c0269504297bcd851dee4775a1a -->
Landed `121fd9a1` (the change: OSC 8 `relay://prose/<pane>/<block>` anchors around every inline block, logical lines + printColumns via `TerminalBackend::setProseBlock`, replacement folds in FoldLayer, one shared break rule `relay::wrap` used by WordWrap/FoldLayer/wrapFoldLines, non-interactive prose URIs, docs) and `b6521ebc` (implementer evidence README in the card's evidence dir). All five fix tasks are implemented. Green on libvterm: `relay-wordwrap-tests` 14/14 (shared rule vs the streamed wrapper, unchanged at print width), `relay-engine-tests` 29+17+29+14+10+9+40/0 including `ViewTest::proseReflowsOnResize` (prints a reply at 80, resizes 48 → 96 → 80, asserts the rows are what the wrapper would print at each width, bullets hanging, inline code and a 25-char word), `relay-markdown-tests` 15/15. Ghostty is OFF in every build dir here (no prefix), so the both-cores row of the QA checklist is left to QA: the ViewTest runs both when built with `RELAY_ENGINE_WITH_GHOSTTY=ON`. One known wart (in the evidence README): search over a taken-over block steps onto the visible match, but the reported count still includes the core's matches on hidden rows. NOTE: my Implemented-By trailers on both commits say `anthropic/claude-opus-4-5` — that is wrong; this pane's model is `glm-5.3` (see the thread stamp). The board's own `implemented_by` stamp is authoritative; please read the trailers as `glm/glm-5.3`.

<!-- relay:entry 20260919T225011Z-t9 author=agent kind=event model=glm-5.3 pane=ca0785f6 turn=f3c4a3de6a60443cb6e8c5c0d285b4bc/871f2c0269504297bcd851dee4775a1a -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented in 121fd9a1 + b6521ebc: prose blocks anchored with OSC 8 and re-wrapped by the view away from the print width; shared break rule; all suites green on libvterm. QA checklist is in the card body; evidence README has the test outputs and the both-cores note for the ghostty row. · evidence docs/qa_evidence/2026-09-19-relays-own-lines-do-not-reflow/ · implemented_by glm/glm-5.3
