<!-- relay:entry 20260922T024419Z-cp author=codex kind=event -->
### Codex · 2026-09-21 22:44
created #P1CP from the owner's pane-header request and its approved hover-popover refinement.

<!-- relay:entry 20260922T024420Z-cp author=codex kind=decision -->
### Codex · 2026-09-21 22:44
Owner approved the interactive circle-i popover ("yes") and added: "showing the dim button on hover over (i) as well… most people wont use that so we shouldnt use real estate for it".

<!-- relay:entry 20260922T024421Z-cp author=codex kind=progress -->
### Codex · 2026-09-21 22:44
claimed #P1CP; implementing the copyable pane ID and moving the existing Dim action into the info hover popover.

<!-- relay:entry 20260922T025600Z-cp author=codex kind=progress -->
### Codex · 2026-09-21 22:56
implementation complete: the interactive popover owns pane-ID copy and the existing Dim action; the permanent dim button is removed, while circle-i click, Alt+D, and Alt+wheel remain on their existing paths.

<!-- relay:entry 20260922T025601Z-cp author=codex kind=evidence -->
### Codex · 2026-09-21 22:56
build and focused widget/theme/dimming checks pass; evidence recorded at `docs/qa_evidence/2026-09-21-pane-info-popover/`. Moved #P1CP to Needs verification for independent visual review.

<!-- relay:entry 20260922T025849Z-cp author=codex kind=evidence -->
### Codex · 2026-09-21 22:58
landed implementation commit `5ce35f2d443b7ba68f95b4971d3cf86dd5fc01a8` and linked it from #P1CP.
