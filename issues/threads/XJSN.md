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

<!-- relay:entry 20260922T020500Z-xs author=codex kind=progress -->
### Codex · 2026-09-22 02:05
Implementing the approved JSON field under the existing claim. Backend storage/preset propagation and the multiline form are in place; omission preserves older callers' saved extra, explicit {} clears. Next: persistence/probe/validation tests and isolated live form-to-HTTP evidence. Board MCP tools are unavailable in this guest harness; using the policy file fallback.

<!-- relay:entry 20260922T020500Z-xt author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T020600Z-xs author=codex kind=evidence -->
### Codex · 2026-09-22 02:06
Moved to needs-verification with Execution Summary and Tests. Focused unittest run: 27 passed;
TestsCommands run 20260922T015843Z-34b3 also records 27 passes and no opened signals.
Build through relay-build passed. Isolated Xvfb add/edit validation, prefill, discovery and
clear succeeded; two actual HTTP captures show parameters present then absent. Evidence:
docs/qa_evidence/2026-09-22-custom-extra-json/README.md. Board format check has no XJSN findings.
Pre-land tests_check has no failing tests; its orphaned warning refers to the earlier decision-only
commit and will be rechecked once implementation is linked. No implementer QA checklist.

<!-- relay:entry 20260922T020600Z-xt author=agent kind=evidence -->
Check · 1 missing-evidence, 1 not-applicable. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T020600Z-xu author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T020600Z-xv author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T020700Z-xs author=codex kind=evidence -->
### Codex · 2026-09-22 02:07
Linked implementation commit 2ae67cfcf27d95b66e70d3097523ff37092f8caa. The selected tree built
successfully through land.py; only the custom-form/include and §28.6 hunks were selected from
shared files. Post-land run 20260922T020115Z-d112: 27 passes, no opened signals. Refreshed
Tests Check: passed, no findings and no failing tests. Independent verification remains pending.

<!-- relay:entry 20260922T021000Z-vr author=codex kind=verification -->
### Codex · 2026-09-22 02:10
Independent root verification passed all 27 custom-provider tests and a separate live form-to-HTTP add/edit/reject/clear drive. Evidence: docs/qa_evidence/2026-09-22-verify-XJSN/README.md. Verdict recorded; moving to done.
