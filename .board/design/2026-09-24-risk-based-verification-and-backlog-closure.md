---
id: P7CF
type: work
status: discussing
labels: [feature, qa, switchboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 3e309d9d-aa32-4dd4-988d-c2105e1b84a9
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: text, primary: person, also: [], human: optional, criteria: 'The research accurately distinguishes evidence of a pass from mere backlog status, and the proposal is actionable.', sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [39b82175e4dc3f20795cbeb2c8106a9d33bd595d], evidence: [reports/AI software verification policy.md, research_notes/AI software verification policy/], related: [C3Q2, EE11], github: null}
---
# Risk based verification and backlog closure

## Issue
can you do deeper research on that issue and put it on a card -- i have 550 cards in needs verification or needs Q&A, and i am sure that most of them are fine.

## Plan
Goal: recommend a closure policy that reduces the verification backlog without losing important quality gates.

Findings: `backend/relay_core/qa_policy.py` defaults to `verification: ask`; `backend/relay_core/board_tools.py` rejects agent closure from verification lanes even after an explicit owner instruction. Related card #C3Q2 built that switch.

Steps:
1. Collect recent primary-source practices and empirical evidence on AI code review, human review, and risk-based QA.
2. Audit the current Board's lane sizes and metadata completeness.
3. Synthesize policy alternatives, a recommended path, safeguards, and a staged backlog reduction plan.

Risks: backlog status alone does not prove a change is good; mass closure needs evidence and rollback.

Verify: source links, reproducible counts, and explicit separation of observed facts from proposed policy.

## Done means
- Compare current primary-source examples of AI-assisted software review and closure, distinguishing review gates from extra UI confirmation.
- Count and classify Relay cards in verification and QA lanes, including evidence and risk markers, without assuming they have passed.
- Record a concrete, risk-based proposal and safe migration path on this card, with sources and unresolved decisions.

## Planning notes
**Research result (2026-09-24):** [Full report](../../reports/AI%20software%20verification%20policy.md) and [reproducible Board audit](../../research_notes/AI%20software%20verification%20policy/relay_backlog.md). The live audit found 547 work cards in the two populated queues: 306 `needs-verification`, 241 `needs-qa-llm`, out of 706 work cards at the final count. Other sessions were editing the Board, so this is a point-in-time count. Only 54/306 and 0/241 have a `verify` block; 13/306 and 7/241 have no structural `unverified_reasons()`. Those 20 are not proven good or mechanically closable: current tests, signals, artifact quality, and policy can still block them. No defensible percentage of the 547 can be presumed safe to close.

**What published practice establishes:** AI-forward teams use different gates. OpenAI reports agents often merging after agent review, with human review optional in one experimental repository; Anthropic says its Code Review does not approve PRs; Amplitude reports automatic merge only for a low-risk class. GitHub can auto-merge once repository checks and eligible approvals pass. None supports a universal extra button press on every completed task. Sources: [OpenAI](https://openai.com/index/harness-engineering/), [Anthropic](https://claude.com/blog/code-review), [Amplitude](https://cursor.com/blog/amplitude), [GitHub](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/automatically-merging-a-pull-request). Empirical work shows AI review can miss vulnerabilities and that broad reviews lose usefulness; a clean AI pass is not proof. Sources: [Amro and Alalfi](https://proceedings.mlr.press/v318/amro26a.html), [Bosu et al.](https://www.microsoft.com/en-us/research/publication/characteristics-of-useful-code-reviews-an-empirical-study-at-microsoft/).

**Recommendation:** Treat verification as a scoped evidence claim and approval as a recorded decision. For low-risk, reversible, precisely specified work, permit closure after an independent verifier confirms a current deterministic or live check covers every `Done means` line; capture revision, command, output, and artifact. Route subjective UI/experience to a staged live probe and accountable judgment. Keep human sign-off for money, privacy/security, deletion, credentials, legal/clinical effects, irreversible external actions, or contested acceptance. Honor an explicit owner instruction in chat to close a named card after showing its evidence and blockers; record the exact instruction, scope, revision, actor, and resulting move in the thread. Do not require a second click. A chat instruction does not impersonate a required formal GitHub review.

**Backlog path:** (1) freeze a manifest of IDs, revision, claims, evidence, signals, and risk; (2) pilot a stratified sample from both lanes, measuring actual defect and missing-evidence rates rather than assuming most pass; (3) process homogeneous low-risk cohorts with shared test runs but per-card assertion receipts; (4) use a declared audit sample to detect a faulty rule, pause that cohort on a material miss, reopen affected cards, and preserve all history. Missing risk metadata remains unknown, not low risk. Defer cards that await an external event; drop duplicates with a reason. Batch status changes alone do not count as verification.

**Proposed implementation decisions:** Add an owner-authorized chat close path to the Board gate; preserve the `ask` setting against autonomous agent closure. Define evidence-complete low-risk auto-close separately from owner authorization. Build a read-only backlog classifier and pilot report before any bulk transition. Review the pilot before setting class thresholds or changing the default QA policy. Related policy implementation: #C3Q2. Prior explicit close request that the current gate refused: #EE11.

## Tests
- Read-only card front-matter audit: reproducible command and limitations in `research_notes/AI software verification policy/relay_backlog.md`.
- Primary-source cross-check: URLs and distinctions in `research_notes/AI software verification policy/team_practices.md` and `review_evidence.md`.
- Report synthesis: `reports/AI software verification policy.md`; no cards were bulk-closed and no product code changed.

## Tasks

- [x] Research published AI-team and platform verification practices <!-- t:tq -->
- [x] Audit Relay verification and QA backlog read-only <!-- t:cr -->
- [x] Write evidence-based policy proposal and migration plan <!-- t:a2 -->
