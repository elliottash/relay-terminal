<!-- relay:entry 20260924T032036Z-q8 author=codex kind=plan -->
### Codex · 2026-09-24 03:20 UTC
Created #Q8TM from the owner's request and added a cross-provider plan. Related work: #1V4F, #0C0V, #3ES1. This card is planned; implementation has not started.

<!-- relay:entry 20260924T033607Z-yd author=agent kind=event model=glm-5.3 pane=bd9e4ae0 turn=9a5b6e44b68e41eaaa1af58945daaa6b/fc31911900e340648176cfcdc9173414 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session bd9e4ae0

<!-- relay:entry 20260924T033607Z-ye author=agent kind=progress model=glm-5.3 pane=bd9e4ae0 turn=9a5b6e44b68e41eaaa1af58945daaa6b/fc31911900e340648176cfcdc9173414 pane_token=bd9e4ae0-caa0-4e43-ba0b-f26212835b63 -->
Claimed (bd9e4ae0) · working on it from a terminal pane

Taking #Q8TM from Codex planning: implement guest-session resume cursors and switch-path accounting per the card's plan.
- id: 20260924T041500Z-q8tm-done
  ts: '2026-09-24T04:15:00Z'
  kind: progress
  by: bd9e4ae0
  body: |
    Delivered the plan on the switch path: per-pane guest-session cursors (durable in the session
    file), relay-managed resume of a guest's own session with a catch-up of only the intervening
    turns, fallbacks (pruned session, stale cursor, explicit resume/fork wins, account keying),
    and switch accounting (model_changed guest_resumed/guest_resume_fallback, status
    resumed_session/catchup_*, turn record fields, logs guest_catchup*). 8 new/changed
    FakeHarness tests + protocol doc. Would-land run 113/113 OK. Evidence:
    docs/qa_evidence/2026-09-24-guest-switch-resume-Q8TM/. Moved to needs-verification.
