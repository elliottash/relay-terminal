<!-- relay:entry 20260923T175935Z-c1 author=codex kind=progress -->
### Codex · 2026-09-23 17:59 UTC
Claimed the report. Traced the failure to `adoptLeafAsTab`: a moved Board keeps its view workspace, while the new tab and helper are unattached. Plan and Done means are on the card.

<!-- relay:entry 20260923T180600Z-c2 author=codex kind=evidence -->
### Codex · 2026-09-23 18:06 UTC
The focused regression passed, the Relay GUI built, and a live Xvfb drag moved the Board to a new tab titled for its project with cards visible. Screenshot: `docs/qa_evidence/2026-09-23-board-move-G4VB/04-moved.png`. The full boardworkspace target has one existing unrelated stale-signature assertion, tracked on the Sphinxpad build gate card. Moved this card to needs-verification for an independent check of the helper's response.
