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
- id: 20260924T045700Z-q8tm-sha
  ts: '2026-09-24T04:57:00Z'
  kind: evidence
  by: bd9e4ae0
  body: |
    Landed as 11847c on main (code, tests, protocol doc, card, evidence). kqnp's concurrent
    usage-reset hunks in the same three files were left uncommitted for their session, per
    land.py's contested-hunk selection. One follow-up observed on the landed tree: the
    catalogue-test race #JXFT now also fires under tests.test_guest_harness_provider +
    tests.test_model_switch (~2/7 runs); evidence appended to that card's thread.

<!-- relay:entry 20260924T041838Z-kn author=agent kind=progress model=glm-5.3 pane=3ecfd0fe turn=9a5b6e44b68e41eaaa1af58945daaa6b/dfbb85ebc93b47538847e1e92dc83175 -->
- ✦ agent is preparing Try it for this card
- id: 20260924T051000Z-q8tm-tryit
  ts: '2026-09-24T05:10:00Z'
  kind: evidence
  by: bd9e4ae0
  body: |
    Try it staged (own staging; no verifying-session stage existed): 
    docs/qa_evidence/2026-09-24-tryit-Q8TM/stage.sh prints the before/after prompt payloads and
    status lines for a guest→native→guest switch on the real code path (FakeHarness + scripted
    native; no model, no network; run twice). Path: 1. agent — run stage.sh, capture
    (capture.txt, exit 0, rerun ok); 2. person — read the two prompts and answer the one
    question (the only judgement: is the catch-up complete and trustworthy; ~2 min). Everything
    else — the suite, the fallbacks, the restart case — is covered by the automated tests and
    was not asked of the person. Saw what was expected: AFTER resumes codex-1 and sends only
    the GLM turn; BEFORE re-sends everything including the guest's own turn.
