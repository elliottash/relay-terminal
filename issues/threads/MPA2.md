<!-- relay:entry 20260922T191545Z-tv author=agent kind=question model=gpt-6-astra pane=eda7f821 turn=678e5e19bfda407591a623f02e9421f6/ba6a76e41c414b65aee73ba7f548100f -->
1. Which models are missing from Priorities on sphinxpad, and do they appear when you type their name in its search box? Recommendation: confirm this before changing list semantics; current code intentionally hides unranked models until search. SSH is presently unreachable.

<!-- relay:entry 20260922T232928Z-b1 author=codex kind=comment -->
### Codex · 2026-09-22 23:29
The owner supplied two specific sphinxpad reports, saved separately as #VPR7 and #CDP7:

> when i added a provider, i had to researt relay to see the model (openrouter) in the priorities tab.

> i couldnt get codex to show up in the priorities tab.

The owner did not say whether searching finds these models.

<!-- relay:entry 20260923T002131Z-m1 author=codex kind=progress -->
Claimed follow-up: user requested tracing, simulations adding/removing models, and a fix. Specific defects already fixed in 3cb7ff33 and 8d03da03; checking those fixes and exercising list operations. Board bridge unavailable; file fallback.

<!-- relay:entry 20260923T002409Z-m2 author=codex kind=evidence -->
Verified existing fixes: three model CTest targets passed; OpenRouter 13 tests and guest-provider 69 tests passed. Full-app worker-event simulation passed. Add/remove/undo and availability behavior covered with temporary QSettings. Evidence: docs/qa_evidence/2026-09-23-MPA2/. Moved to needs-verification; sphinxpad unreachable, not redeployed.
