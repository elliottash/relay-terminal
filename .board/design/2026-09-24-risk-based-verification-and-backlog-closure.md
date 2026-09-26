---
id: P7CF
type: work
status: planned
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
Goal: implement the two gates the research and the #0FBB example named — owner-authorized chat close and independent-verifier `ai-visual` gating — and the first two steps of the backlog path (manifest + pilot sample), leaving `verification: ask` strong against autonomous closure and moving no backlog card.

Findings:
- `backend/relay_core/qa_policy.py` — `apply()` rule 2 demotes an `ai-text`/`ai-visual` primary to `also` unless `ai_may_gate_after` is satisfied **and** the card's `qa` block names a verifier outside the implementer's lineage; the default `ai_may_gate_after: never` demotes every AI primary, so `ai-visual` never gates. `closes_automatically()` is true only under `verification: automatic` with `human` not `required`.
- `backend/relay_core/board_tools.py` `_qa_ask_gate` (~line 2227): under `ask`, a verifying session moving a card from `needs-verification` or a `QA_STATUSES` lane to `done` raises `board_refused` with `requires: user_close`, `offer: needs-verification`; only `OWNER_ACTOR` is exempt — where the #0FBB close died.
- `_verify_gate` (~line 3619): `human: required` refuses `done` for an agent until a `## Human QA` question carries an `Answer:` line; `verify.deferred` and `sign_off` receipts refuse for anyone. `_human_qa_gate` refuses any close with an unanswered `## Human QA` question (#WC3E).
- `_move` (~line 2606) funnels every move through `_signal_gate`, `_human_qa_gate`, `_verify_gate`, `_qa_ask_gate`; its `allowed` args set is where a new argument registers, and `self._append(card, text, kind=...)` is how a thread entry lands (`_comment`, ~line 2970; `decision` entries must carry a quotation).
- Backlog audit (2026-09-24, `research_notes/AI software verification policy/relay_backlog.md`): 306 `needs-verification` + 241 `needs-qa-llm`; 54/306 and 0/241 carry a `verify` block; 20 have no structural `unverified_reasons()`.

Steps:
1. **Owner-authorized close.** `board_move_card` gains `authorized_by` — the owner's verbatim chat instruction, one card per call. In `_qa_ask_gate`, a close to `done` carrying `authorized_by` passes under `ask`: the same write appends a `decision` thread entry quoting the instruction with date, relaying session, and the revision the evidence was shown at (skip it if that exact quote is already on the thread), and the result's `note` reads `closed on the owner's instruction: …`. Without `authorized_by` the refusal stays word for word; `verification: automatic` behaviour is untouched.
2. **Authorization satisfies the person gates.** A recorded owner authorization also passes `_human_qa_gate` and `_verify_gate`'s `human: required`: the move writes the owner's words as an `Answer:` line under the open `## Human QA` question, attributed (`Answer: owner, relayed by <session>: "…"`). `verify.deferred` and `sign_off` receipts still refuse — a chat instruction cannot create a dated receipt or end a deferral.
3. **Independent `ai-visual` gating.** In `qa_policy.apply`, split rule 2: `ai-visual` keeps `primary` when the card's `verify.artifact` is `visual` **and** the `qa` block names a verifier outside the implementer's lineage — no `ai_may_gate_after` needed. `ai-text` keeps today's rule exactly; `ai-visual` without an independent verifier still demotes with today's note.
4. **Backlog manifest + pilot sample.** New read-only `scripts/board_backlog.py`: walk `.board/` work cards in the two lanes, emit per-card rows (id, status, implemented_by, session age, `verify` summary or none, evidence paths, `links.commits`, `unverified_reasons()` signals, last thread entry) to `research_notes/AI software verification policy/backlog-manifest-<date>.md`, plus per-stratum counts and a fixed-seed stratified pilot sample (20 per lane). `--json`; writes nothing to `.board/`.
5. **Docs.** `docs/BOARD-FORMAT.md` (`qa:` policy block; `authorized_by` on `board_move_card`), `docs/AGENT-SESSIONS-PROTOCOL.md` 19.21 (the AI-gating split, `authorized_by`, one-card scope, what lands on the thread), one sentence on the Verification row in `docs/ARCHITECTURE.md`.

Risks: `authorized_by` is the relaying session's claim, not a channel the backend can verify — a dishonest agent could invent a quote; the mitigations are one card per call, the permanent decision entry naming relayer and revision, and refuse-by-default. Step 3 relaxes a default #C3Q2 set on your steer; say the word if you want it behind a policy key (`ai_visual_may_gate:`) rather than unconditional. The manifest is point-in-time in a board other sessions edit. No backlog card changes status here — cohort closing waits for your review of the pilot.

Verify: extend `tests/test_qa_policy.py` (visual + independent verifier keeps `ai-visual` primary; without independence it demotes; `ai-text` unchanged) and `tests/test_board_tools.py` (authorized close under `ask` appends the decision entry and the `note`; refusal unchanged without it; `human: required` + authorization writes the `Answer:` line; `deferred`/receipt still refuse) — `python3 -m pytest tests/test_qa_policy.py tests/test_board_tools.py -q`, plus `tests/test_board_protocol.py` if `authorized_by` reaches `tools/list`. Run `scripts/board_backlog.py` twice: identical output, counts near the audit's 306/241 (point-in-time). Land through `scripts/land.py` as usual.

## Done means
- An agent can close one named card on the owner's explicit chat instruction while `verification` is `ask` — the move carries the verbatim quote — and the card's thread records the quote, who relayed it, and the revision; this is the #0FBB path, which today refuses with `requires: user_close`.
- Without that instruction, an agent close out of `needs-verification`/QA lanes still refuses exactly as today, and `verify.deferred` and sign-off receipts still block anyone.
- A visual card whose verifier is outside the implementer's lineage keeps `ai-visual` as its primary rung instead of being demoted to `also`.
- A read-only backlog classifier reproduces the lane manifest (IDs, verify blocks, evidence, signals) and a fixed-seed pilot sample, changing no card status.
- Failure looks like: the owner says "that verifies it for me" and the agent is still refused — or a backlog card moves status with no recorded instruction.

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
