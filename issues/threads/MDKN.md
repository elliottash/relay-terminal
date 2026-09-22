<!-- relay:entry 20260921T073000Z-a0 author=agent kind=event pane=linklabels -->
- ✦ card created from #AGNT's QA checklist item "A markdown link's label is not clickable" · issues/changes/2026-09-21-a-markdown-links-label-is-not-clickable.md

<!-- relay:entry 20260921T073000Z-a1 author=owner kind=decision pane=linklabels -->
- ✦ owner decision: "yes, add the linking" — a markdown link's label must be clickable in every agent transcript, terminal panes and embedded consoles alike, not only the target printed after it.

<!-- relay:entry 20260921T073100Z-a2 author=agent kind=event pane=linklabels -->
- ✦ claimed by the `linklabels` session

<!-- relay:entry 20260921T081500Z-a3 author=agent kind=progress pane=linklabels -->
- ✦ landed in five commits: `a7ba08d8` the `#l=` convention and the renderer, `70a288c9` the view (the hit test and the merged anchor), `195f2442` the pane and the reasoning fold, `19beea01` the docs and the copy case, `620d14e7` the evidence. Design: the label's cells carry the block's own prose anchor with the target as a fragment, so no change reaches either emulator core — which the alternatives (a private anchor mark, a second per-cell link layer) could not avoid, and only libvterm builds on this machine.

<!-- relay:entry 20260921T081600Z-a4 author=agent kind=progress pane=linklabels -->
- ✦ moved to needs-verification. `docs/qa_evidence/2026-09-21-markdown-link-labels/` — 41 PASS · 0 FAIL: the label clicked for all five kinds in a terminal pane, in two embedded consoles, after a re-wrap, inside a thinking bubble, and the printed target after a restart. A transcript full of links renders to the identical picture with the card on and off (0 pixels, whole window), measured against the same tree with the feature switched off.

<!-- relay:entry 20260922T005810Z-y8 author=agent kind=note pane=switchboard -->
Handoff: markdown link labels are clickable everywhere (not just the printed target beside them), via an OSC 8 run nested in the prose block's own fold anchor — no engine/core change, so it works on libvterm as-is and should be fine on GhosttyCore though that core doesn't compile on this machine to check. 41/41 driven by the implementer; QA checklist asks for the same by hand (label click, re-wrap, fold, hover tooltip, copy, restart).
