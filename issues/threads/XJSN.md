<!-- relay:entry 20260922T005909Z-a1 author=claude-code kind=note -->
### Claude Code · 2026-09-22 00:59
Filed by Claude while retiring the advanced provider dialog (#MDL1 t:a15, cdc4b9bb): the per-request JSON body had no other home. Built-in providers are unaffected.

<!-- relay:entry 20260922T010640Z-p1 author=codex kind=progress -->
### Codex · 2026-09-22 01:06
Claimed at the owner’s request to claim and deliver these cards. Reviewing the implementation paths while awaiting the product decisions below.

<!-- relay:entry 20260922T010640Z-q1 author=codex kind=question -->
### Codex · 2026-09-22 01:06
1. Recommend adding an optional JSON-object field to the custom-provider form and carrying it through to requests. Should we implement this, or drop the card?
Moved to discussing, waiting_on owner.

<!-- relay:entry 20260922T010803Z-r1 author=codex kind=plan -->
### Codex · 2026-09-22 01:08
Recorded implementation findings, provisional Done means and a checkable plan. No implementation or deployment while the product decision is pending.

<!-- relay:entry 20260922T010911Z-s1 author=codex kind=progress -->
### Codex · 2026-09-22 01:09
Confirmed the transport allowlist: thinking, reasoning, reasoning_effort, temperature, top_p. Updated the plan to validate before saving and preserve the current contract.

<!-- relay:entry 20260922T014800Z-rn author=codex kind=progress -->
### Codex · 2026-09-22 01:48
Owner requested valid card codes: #MDL1 → #MDP1 and #RLP7 → #RPR7. Updated this card’s current references to the renamed cards; prior thread entries are unchanged.

<!-- relay:entry 20260922T015132Z-ds author=codex kind=decision -->
### Codex · 2026-09-22 01:51
> add the extra json

Implement the optional JSON-object field on custom providers, preserving the transport's existing supported-parameter contract.

Resumed execution under the existing claim.
