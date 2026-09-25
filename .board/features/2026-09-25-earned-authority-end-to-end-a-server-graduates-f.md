---
id: GW74
type: work
status: discussing
labels: [feature, qa, skills, worker, switchboard]
component: [worker, board]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'a server with the configured streak has its next case sampled at the configured rate, one fail resets it, and an AI verdict a person overturned lowers the agreement count', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'owner, Relay conversation, 2026-09-23 and 2026-09-25; #1QKM §1 and §5 point 3, #C3Q2''s thresholds'
links: {plans: [], commits: [], evidence: [], related: [1QKM, C3Q2, 95VZ, 1AA6, SJTR, BX7B], github: null}
---
# Earned authority end to end: a server graduates from review-each to sampled to AI-gated on its record, is demoted on a fail, and AI verdicts are calibrated against a person's

## Issue
agent should make this suggestion, and also the effort level, per task. per project, or per globals (or options?), user can decide whether its automatic or not [...] ideally, most of this is just in the agent's work and the user doesn't see it directly [...] analyze these issues and add comprehensive plans to cards. clarify anything with me with questions.

## Plan
**Goal.** Make earned authority real. #C3Q2 compares `ai_may_gate_after` / `sample_after` against the count of passing cases, but nothing decides whether *this* case is the sampled one, nothing demotes a server after a fail, and "passes" is not calibration: an AI rung earns the right to gate by agreeing with a person, not by passing. This card adds a pure `authority` computation over the ledger, applies it where the verify block is proposed, and records the person-versus-AI agreement the rest depends on. Agent-facing throughout (owner, 2026-09-23); the person meets it as one word on the skill page (#9FX8): *review each* / *sampled 1 in k* / *AI gates*.

**Findings.**
- `qa_policy.apply` (`backend/relay_core/qa_policy.py`): rule 2 moves an AI `primary` to `also` unless `_allowed(ai_may_gate_after, cases)` and the `qa` block names an independent verifier; rule 3 drops `sample` unless `_allowed(sample_after, cases)`. `cases` is `BoardTools._verified_cases`: passing rows for the card's server, else across the board.
- `cases.stats` gives `pass_rate_30` and `stale`; no streak, no agreement. Rows carry `signal {mode, result}` (the rung that judged and what it said) and `verdict {result, who, revision}` (who decided), so agreement is computable: rows where `signal.mode` is `ai-text` / `ai-visual` and `verdict.who` is `person` compare `signal.result` to `verdict.result`.
- Rows come from: a `## Verdict` write (`record_case` with `verdict_from_text`), a move (`pass` if done else `fail`), Try it (`served_by: person`), turn end (`pending`). `human_qa_answer` (#1AA6) records the person's answer on the card; whether it writes a ledger row is checked in step 4.
- `sample` on the verify block is a free-text line today (`PROFILE_TEXT`); nothing parses `1/5`.

**Steps.**
1. **`cases.authority(records, server, policy, *, now)`** → `{verified, streak (consecutive decided passes, newest first), fails_30, agreement: {n, agree, fp, fn}, level: 'review-each' | 'sampled' | 'ai-gates', k}`. Rules, all from the policy: `sampled` when `streak >= sample_after` and no fail in the last 30 decided rows; `ai-gates` when `agreement.agree >= ai_may_gate_after` and `fp / n <= max_fp` (policy key, default `0.1`) and the `qa` block names an independent verifier; **demotion**: any `fail` resets `streak` to 0 (back to review-each until the streak rebuilds) and an fp over the ceiling drops `ai-gates`. Pure, tested with synthetic ledgers.
2. **Apply at proposal time.** `BoardTools._qa_floor` passes the server's `authority` instead of a bare count. Rule 2 uses `level == 'ai-gates'`; rule 3 keeps `sample` when `level == 'sampled'`, normalises it to `1/k` (`k` from the policy, default 5), and **picks**: `int(sha256(case_or_card_id), 16) % k == 0` → this case is the sampled one and `human` is raised to `required` with the profile's `criteria`; otherwise `human` becomes `optional` and the note says why. Deterministic, so a re-read proposes the same thing. Notes stay in the tool result and thread event.
3. **Counters mean agreement.** `ai_may_gate_after: <N>` counts AI verdicts a person later confirmed (`agreement.agree`), not passes; `sample_after: <N>` counts the streak. `qa_policy` docstring, `effective_line` and `docs/AGENT-SESSIONS-PROTOCOL.md` 19.21 say so; defaults stay `never`. `board.yaml qa:` gains `max_fp` and `sample_k`.
4. **The person's verdict lands as a row.** When `human_qa_answer` records an answer that decides a card (#1AA6) or a `## Verdict` is written by a person (the Review pane, #BX7B), `record_case` writes a row with `verdict.who: person` and the `signal` the AI rung left — the pair step 1 compares. If the AI rung's row was written first with `verdict.who` a model signature, the person's row carries `revises: <row id>` (a new optional key) so agreement counts the pair once. Tests: an AI pass overturned by a person counts one fp.
5. **Show one word.** `skills_list` items and the registry row (#9FX8) carry `authority.level` and `streak`; the skill page shows "review each · streak 12" or "sampled 1 in 5"; the Review pane's row for a sampled case says "sampled" in its reason line. Nothing else drawn.
6. **Docs.** Protocol 19.21 (levels, `max_fp`, `sample_k`, the pick), 19.22 (`revises`), `board_policy.md` rule 11 one clause ("sampled cases are the policy's, not the agent's, choice").

**Risks.**
- Small numbers: with `ai_may_gate_after: 30` and one fp the rate is 3%; with 5 rows one fp is 20% — the ceiling needs a minimum `n` (use `ai_may_gate_after` itself as the minimum).
- Gaming by the author: the streak counts *decided* rows; a `pending` turn row never counts, and the author's own `done` move on a medium card counts as a pass for `card:<ID>` servers only, never for a skill (already the `_verified_cases` rule).
- A person-served skill (no AI rung) is sampled on its human step only; `ai-gates` never applies to it.
- Owner decisions (thread): default `k`, the fp ceiling, whether demotion resets to zero or halves.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_cases.AuthorityTests tests.test_qa_policy tests.test_board_tools.VerifyGateTests`; a staged board with `sample_after: 3`, three passing rows, then a fourth card whose proposed block carries `sample: 1/5` and the pick; a fail row, and the fifth back to `human: required`.

## Done means
- `cases.authority` returns streak, agreement (n, agree, fp, fn) and a level of review-each / sampled / ai-gates from the policy's thresholds; one fail resets the streak, an fp rate over the ceiling drops ai-gates.
- A card served under a sampled server gets `sample: 1/k` and a deterministic pick: the picked case has `human: required`, the others `optional`, with the reason in the tool result only.
- A person's answer or verdict on a card that an AI rung judged lands as a ledger row paired with the AI's, so agreement is computed from real pairs.
- The skill page shows the level as one word; nothing else is drawn.
- Failure looks like: a sampled pick that changes on re-read, a streak that survives a fail, or an AI rung gating with no independent verifier named.
