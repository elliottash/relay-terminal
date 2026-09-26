<!-- relay:entry 20260925T195148Z-j3 author=agent kind=event model=glm-5.3 pane=22f05421 turn=46dc7f7d90f746138bbd7773c040726f/8158a5244fe84edc98566c4b1ad2f062 -->
- ✦ agent created this card in Planned · .board/features/2026-09-25-moveable-split-between-an-artifact-and-its-docke.md

<!-- relay:entry 20260925T195201Z-72 author=agent kind=note model=glm-5.3 pane=22f05421 turn=46dc7f7d90f746138bbd7773c040726f/8158a5244fe84edc98566c4b1ad2f062 -->
Where this sits: #PBZ4 landed the docked agent console under a file (4a5a4267, ArtifactContext); #2FQ9 is the queued pop-out into a linked pane. This card is the middle path — resize in place. Likely shape: the docked console's share of the pane becomes a real draggable split (the tab-level splitters already exist, `newSplitter` in src/RelayWindowCore.cpp), the chosen share persists per pane in the layout node alongside `split` sizes, and double-click on the handle snaps back to the default. It must not reintroduce the jiggle #2026-09-20-pane-sizes-jiggle fixed, and the collapsed "Agent (Alt+Q)" row state still wins over a dragged size. Builder: read #PBZ4's thread and src/PaneChrome.h's docked-console container first; verify a target before implementation and write `## Done means` before code.

<!-- relay:entry 20260926T001521Z-dh author=agent kind=event model=gpt-6-sol pane=12e75122 turn=b3e2dbb9edab410ba0556c7fbebc38ba/8df532c4ea6547e0aab524e2fff5558c -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260926T001526Z-p6 author=agent kind=evidence model=gpt-6-sol pane=12e75122 turn=b3e2dbb9edab410ba0556c7fbebc38ba/8df532c4ea6547e0aab524e2fff5558c -->
Owner reports the docked-agent size cannot be adjusted in the Card pane: “thats not working in the card pane.” Include a standalone Card pane in implementation and live verification of the draggable divider.

<!-- relay:entry 20260926T001923Z-9t author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260926T001923Z-9u author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260926T002041Z-r8 author=agent kind=event model=gpt-6-sol pane=switchboard turn=5dafa0c7c3cd7c92a11f378da0b4b73e/6dda970969904f3d83542004637b95de -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260926T002103Z-m7 author=agent kind=event model=gpt-6-sol pane=switchboard turn=5dafa0c7c3cd7c92a11f378da0b4b73e/6dda970969904f3d83542004637b95de -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260926T002108Z-zs author=agent kind=comment mode=plan model=k3 turn=5dafa0c7c3cd7c92a11f378da0b4b73e/6dda970969904f3d83542004637b95de -->
I’ll read the card and linked context, inspect the docked pane code, then update only the card’s “Done means” and “Plan” sections.I’m using the Board delivery guidance for this planning turn. I’ll keep all edits on #ZPHJ.

The file dock currently fixes its expanded height with a 45% cap. I’m tracing the Card pane’s separate layout and the saved pane state so the plan covers the reported Card pane behavior and persistence.

The Card page embeds its agent directly under the document and caps it at 40% of the Board view; it has no divider of its own. The plan will give the Card page an internal splitter and save its size in that pane’s layout node.

#ZPHJ now has revised “Done means” and a plan covering draggable splits in file and standalone Card panes, saved sizes, and fold/unfold behavior. No owner decision is needed; it’s ready for Run.

<!-- relay:entry 20260926T002108Z-zt author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260926T011423Z-my author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session ca806657

<!-- relay:entry 20260926T011423Z-t9 author=owner kind=progress pane=switchboard pane_token=ca806657-fe9e-4354-bea5-0f77784e2ee7 -->
Claimed (ca806657) · working on it from a terminal pane

<!-- relay:entry 20260926T011647Z-p5 author=agent kind=progress model=claude-opus-5-5 pane=ca806657 turn=9ba7b3c39cee4d7aaa6643dd0a7c70e1/ee98636e89004335af57156772a1b5fe -->
Started. Approach: one shared `relay::AgentSplit` (vertical QSplitter subclass, `src/AgentSplit.h`) that keeps the agent's share across resizes, records only user drags (programmatic setSizes never saves), and sizes a folded agent to its one row with the handle disabled. Used by FilePreview, PlanEditor and CardDetail; the share is saved as `agent` on the preview/plan/card leaf and restored in buildNodeWidget.

<!-- relay:entry 20260926T013637Z-kp author=agent kind=event model=claude-opus-5-5 pane=ca806657 turn=9ba7b3c39cee4d7aaa6643dd0a7c70e1/ee98636e89004335af57156772a1b5fe -->
- ✦ agent moved this card · Running → Needs verification, wrote `## Execution Summary`, `## Tests` · Landed c80f0c80: draggable artifact/agent divider in file, plan and Card panes, saved per pane; tests and live evidence attached. · evidence docs/qa_evidence/2026-09-25-zphj-agent-divider/ · implemented_by anthropic/claude-opus-5-5 via claude-code
