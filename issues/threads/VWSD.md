<!-- relay:entry 20260920T121323Z-br author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-as-the-window-narrows-and-tab-headers-dont-fit-r.md

<!-- relay:entry 20260920T121325Z-ac author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T121325Z-nh author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T121917Z-vp author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=d0810f378d6c430083b16350f9ec7084/e6a3248ef5f94773aee5700f39273dd0 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T121921Z-9q author=agent kind=comment mode=plan model=deepseek/deepseek-v4.1-flash turn=d0810f378d6c430083b16350f9ec7084/e6a3248ef5f94773aee5700f39273dd0 -->
I'll start by reading the card and finding the code it's about.



Let me find the tab bar and where these indicators are rendered.









































Now I have what I need. Writing the plan.



**#VWSD now has a plan.** It adds a pure, testable rule (`relay::usage::tabMetersFit`, next to `tabSuffix()`) plus a `tabMetersHaveRoom()` in `RelayWindow.h` that compares the bar's usable width against the tabs' *full* label widths, gates the suffix in `tabLabelText()` (6540) on it, relabels only when the answer flips (hooking the tab bar's existing `Resize` event filter at 835), and leaves the tooltip and the 5 s clock's rolling mean intact so the reading returns current.

One thing I'd like from you, on the card under Risks: I've assumed the whole `cpu · mem` suffix goes at once, as your words ask — the alternative would be a middle rung (`cpu` alone, like the pane header's ladder), and I'll build the whole-drop version unless you say otherwise. Everything else in the plan is the implementer's to decide; nothing blocks Execute.

<!-- relay:entry 20260920T121921Z-9r author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T122004Z-1e author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T122004Z-yt author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T122005Z-gf author=owner kind=progress pane=switchboard pane_token=c61fed73-3138-4fad-b91a-94c570e63720 -->
Executing (c61fed73) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T124608Z-2a author=agent kind=event model=glm-5.3 pane=c61fed73 turn=bb2d13c855b94b2c87c91ced1e437da6/a75c78b14a3e49a4a0b291a0d25f33b9 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["ab3b96c3", "03e11b91"], "evidence": ["docs/qa_evidenc…; appended to `## QA checklist`

<!-- relay:entry 20260920T124619Z-da author=agent kind=progress model=glm-5.3 pane=c61fed73 turn=bb2d13c855b94b2c87c91ced1e437da6/a75c78b14a3e49a4a0b291a0d25f33b9 -->
Landed. ab3b96c3 adds the give-way rule (`relay::usage::tabMetersFit` + `tabMetersHaveRoom()` gating `tabLabelText()`, Resize-driven relabel, docs and unit/source-text tests); 03e11b91 records the Xvfb evidence in `docs/qa_evidence/2026-09-20-tab-meter-give-way/` (width ladder 900→420 and back, steady-label takes at 420). Built with scripts/relay-build; targeted paneusage/titles/boardworkspace tests passed. Ready for verification.

<!-- relay:entry 20260920T124628Z-3g author=agent kind=event model=glm-5.3 pane=c61fed73 turn=bb2d13c855b94b2c87c91ced1e437da6/a75c78b14a3e49a4a0b291a0d25f33b9 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented per plan; commits ab3b96c3 + 03e11b91, tests green, Xvfb evidence recorded. · evidence docs/qa_evidence/2026-09-20-tab-meter-give-way/ · implemented_by glm/glm-5.3
