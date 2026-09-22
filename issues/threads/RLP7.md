<!-- relay:entry 20260922T005909Z-a1 author=claude-code kind=question -->
### Claude Code · 2026-09-22 00:59
Filed from the owner's ask while working #MDL1. Three questions on the card: one shared code or one per person, which upstreams Pro serves, and whether Pro replaces relay free in the lite rule. Relay Free's own mapping is in docs/RELAY-FREE.md; the live one is the gateway's config on elliott-main-1.

<!-- relay:entry 20260922T010640Z-p1 author=codex kind=progress -->
### Codex · 2026-09-22 01:06
Claimed at the owner’s request to claim and deliver these cards. Reviewing the implementation paths while awaiting the product decisions below.

<!-- relay:entry 20260922T010640Z-q1 author=codex kind=question -->
### Codex · 2026-09-22 01:06
1. Access codes: recommend one per person, so a single code can be revoked. Shared code instead?
2. Which upstream model IDs should serve Pro high/main/flash (and lite if applicable)?
3. Lite: recommend keeping Relay Free for background lite jobs by default, with Pro available as an explicit override. Should Pro replace Free instead?
Moved to discussing, waiting_on owner.

<!-- relay:entry 20260922T010803Z-r1 author=codex kind=plan -->
### Codex · 2026-09-22 01:08
Recorded implementation findings, provisional Done means and a checkable plan. No implementation or deployment while the product decision is pending.

<!-- relay:entry 20260922T010911Z-s1 author=codex kind=progress -->
### Codex · 2026-09-22 01:09
Read-only attempt to inspect the live upstream mapping failed: ssh could not resolve elliott-main-1. No production change made. docs/RELAY-FREE.md records GLM 5.3 Flash for main/high, DeepSeek V4.1 Flash for flash and Gemini 3.5 Flash Lite for lite via OpenRouter; that documentation is not proof of the current deployed mapping.
