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

<!-- relay:entry 20260922T014800Z-rn author=codex kind=progress -->
### Codex · 2026-09-22 01:48
Owner requested valid card codes: #MDL1 → #MDP1 and #RLP7 → #RPR7. Renamed this existing card and its thread; retained its old ID in aliases. Earlier thread entries remain byte-for-byte intact. Updated current card cross-references and documentation; historical evidence paths and source comments retain the original codes. No change to status, ownership, task completion or outstanding decisions.

<!-- relay:entry 20260922T015132Z-ds author=codex kind=decision -->
### Codex · 2026-09-22 01:51
> 1 pro - per person
> 2 i think there should be two versions actually.
>
> relay pro could be glm 5.3 (high / main), glm 5.3 flash (flash)
>
> relay ultra could be astra or fable -- lets defer that

Implementation: per-person revocable codes; Pro high/main use GLM 5.3 and flash uses GLM 5.3 Flash. Ultra is explicitly deferred. Lite retains Relay Free (the prior recommendation; no Pro lite was requested). Keep current quota/spend ceilings unless configured otherwise; no billing or production deployment in this change.

Resumed execution under the existing claim.

<!-- relay:entry 20260922T021355Z-tz author=agent kind=evidence -->
Check · 7 missing-evidence, 1 not-applicable, 5 passed; 7 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T021539Z-dm author=agent kind=evidence -->
Check · 1 not-applicable, 12 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T021612Z-dv author=codex kind=progress -->
### Codex · 2026-09-22 02:16
Implemented Pro personal codes, gateway checks and revocation, desktop activation/removal, GLM high/main/flash defaults and Free lite. Final live isolated Relay passed activation, generation, denial after revocation with no upstream request, and keyring removal. 281 targeted backend tests, 63 effort follow-up tests and all 3 C++ test groups pass. tests_check has no failures or signals (only existing timing notices). Evidence docs/qa_evidence/2026-09-22-RPR7/README.md. Landed to needs-verification; separate Relay verifier a5 is checking final implementation. No production deployment or production code issuance.

<!-- relay:entry 20260922T021833Z-qa author=codex kind=evidence -->
### Codex · 2026-09-22 02:18
Independent verifier a5 reports PASS, evidence commit 50c23e65. Root reviewed the checklist, tests and live screenshots: valid activation/Pro turn succeed; revocation denies next turn with no upstream increase; Free/BYOK and credential guards pass. Copied verifier checklist/verdict to the card and closed implementation as done. Production rollout remains separate and is documented in gateway/README.md. XJSN also independently verified and closed.
