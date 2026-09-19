<!-- relay:entry 20260919T050222Z-8q author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent created this card in Ready · issues/features/2026-09-19-fail-over-to-another-provider-when-one-keeps-fai.md

<!-- relay:entry 20260919T050233Z-cc author=agent kind=question model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
Which providers may a failing turn fall over to? My recommendation: only ones already usable without any setup — other presets with a stored key (order: the pane's own provider first, as today; then other keyed presets; then Relay Free as the last resort), never a provider that would silently spend a key the user did not choose. The switch would happen after the transport's retries are exhausted (six today, on 429/5xx only), announce itself in the transcript the way provider_retry does, and last for the rest of the turn only — the pane's own provider is not reconfigured. Answer here or in the terminal and I'll build it.

<!-- relay:entry 20260919T050242Z-2q author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260919T054939Z-er author=agent kind=decision model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
"yes exactly: other keyed presets from the same tier (main / flash), then Relay Free. this can be disabled in the models options page" — pane 1, 2026-09-19.

<!-- relay:entry 20260919T054948Z-dd author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · waiting_on: owner → (unset)

<!-- relay:entry 20260919T054956Z-e0 author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · appended to `## Decisions`

<!-- relay:entry 20260919T054959Z-rt author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent moved this card · Ready → In progress · Owner answered the candidate question; building it now.

<!-- relay:entry 20260919T055415Z-a8 author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · appended to `## Tasks`

<!-- relay:entry 20260919T055420Z-bx author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T055426Z-bv author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/5bb6146181b74cf69fcec26aab58a1ca -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented on the exact tree (build + backend tests green); checklist and evidence on the card. · evidence docs/qa_evidence/2026-09-19-provider-failover/
