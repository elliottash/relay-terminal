<!-- relay:entry 20260919T050222Z-8q author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent created this card in Ready · issues/features/2026-09-19-fail-over-to-another-provider-when-one-keeps-fai.md

<!-- relay:entry 20260919T050233Z-cc author=agent kind=question model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
Which providers may a failing turn fall over to? My recommendation: only ones already usable without any setup — other presets with a stored key (order: the pane's own provider first, as today; then other keyed presets; then Relay Free as the last resort), never a provider that would silently spend a key the user did not choose. The switch would happen after the transport's retries are exhausted (six today, on 429/5xx only), announce itself in the transcript the way provider_retry does, and last for the rest of the turn only — the pane's own provider is not reconfigured. Answer here or in the terminal and I'll build it.

<!-- relay:entry 20260919T050242Z-2q author=agent kind=event model=glm-5.3 pane=7ee5d45b turn=6ffe5e5d778c463194abca45138bb113/184b22cd9a934409978320abb17637a1 -->
- ✦ agent updated this card · waiting_on: (unset) → owner
