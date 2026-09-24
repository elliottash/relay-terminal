<!-- relay:entry 20260922T132116Z-s2 author=agent kind=evidence model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/69757fa04bba4791b0b2c06e1605b039 -->
Analyzed session ef7ab5c3ef21445ab80dea50fedfbb29 (pane a352a041). Saved timeline and extracted logs at docs/qa_evidence/2026-09-22-model-session-audit/. Reproduced guest role transport URL failure and fallback-error suppression without network calls. Fixes remain inbox; no application changes. Exact target of the rejected role pick is absent from logs.

<!-- relay:entry 20260922T132116Z-s3 author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/69757fa04bba4791b0b2c06e1605b039 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260922T142919Z-3d author=agent kind=progress model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
Claimed via file fallback as codex. Delivering model-switch fixes, picker logging, fallback error diagnostics and live GUI evidence. User request: "deliver fixes to these issues and verify them with a live drive. also try to log what the box picker says when you first open a pane, and when you use it to change the model"

<!-- relay:entry 20260922T142919Z-9c author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T142919Z-9d author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T145029Z-yh author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T145511Z-p3 author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T145831Z-v8 author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-model-ses… → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-model-ses…; replaced `## Execution Summary`

<!-- relay:entry 20260922T145831Z-v9 author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and tested; live GUI drive and picker logs saved for independent verification. · evidence docs/qa_evidence/2026-09-22-model-switch-fixes/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T150313Z-06 author=agent kind=event model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-model-ses… → {"plans": [], "commits": ["552982fa58c6f82a0999452888e88f980b37c22e"], "evidence…

<!-- relay:entry 20260922T150313Z-w8 author=agent kind=evidence model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
Landed implementation and live-drive evidence in 552982fa58c6f82a0999452888e88f980b37c22e. land.py built the exact committed tree successfully using scripts/relay-build --target relay, excluding concurrent sessions' uncommitted changes. Build-gate record: docs/qa_evidence/2026-09-22-model-switch-fixes/land-build.txt. Awaiting independent verification.

<!-- relay:entry 20260922T165157Z-p2 author=codex-hg26-p2 kind=progress -->
Read complete card/thread and implementation 552982fa. Atomic transport switching and truthful fallback diagnostics already landed; retaining these changes and running focused regressions while completing YJG7 reset-aware suppression. No duplicate implementation.

<!-- relay:entry 20260922T165621Z-p2 author=codex-hg26-p2 kind=evidence -->
Follow-up complete; needs-verification. 220 targeted tests and all 14 isolated GUI stages pass; scripts/relay-build --target relay passed. Existing switching/fallback implementation preserved; new quota suppression and failed guest→native transport test. Evidence: docs/qa_evidence/2026-09-22-quota-suppression/report.md. No HG26 or logging edits.

<!-- relay:entry 20260922T165701Z-r2 author=codex-hg26-p2 kind=evidence -->
Landed follow-up in 24a6b202d8121b35173b9aabe456c9c7eb32a72c through land.py; added commit link. Still needs independent verification.
