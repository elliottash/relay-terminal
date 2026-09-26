---
id: 4QM4
type: work
status: deferred
labels: [feature, research, qa, switchboard]
component: [worker]
milestone: beta
workstream: agent
rank: zzzzzzzr
created: '2026-09-19'
acceptance: 'a per-item success matrix for >=2 reviewers over >=2 author models on CodeJudgeBench, with detection and false-approval rates, pairwise phi between reviewers, a per-bug-type breakdown, and a stated answer to whether the best reviewer depends on the author; the result either keeps or changes VERIFIER_RANK''s ordering, with the reason recorded on #T71W'
source: 'owner, in the terminal, 2026-09-19, after the #T71W research passes found the question unanswered in the literature'
links: {plans: [], commits: [], evidence: [], related: [T71W], github: null}
---
# Measure the author x reviewer matrix: for Claude-made bugs, is Kimi or GLM the better reviewer?

## Planning notes

Owner, 2026-09-26: "4qm4 defer". Do not run the metered pilot now. Revisit only when the owner
asks to resume the study; then refresh provider availability and monetary pricing before seeking
a capped spend decision. The no-spend design on this card remains the starting point.

## Issue
add the pilot as an issue card

(asked across 2026-09-19, in these words: "can you see if there is existing research on verifier
rankings, and correlation of errors/capacities across models?" / "i meant, what is the cross-model
correlation of review capabailities, at the task level. so like, for a given bug type, is kimi
success more correlated with claude or codex. if that doesnt exist, thats a paper" / "and more
specifically, for claude-made bugs, is kimi or glm better at reviewing them" / "(but author x
author and reviewer x reviewer correlations are also informative here)" / "i meant item-level
pairwise correlations between models")

## Why this card exists

Two research passes for [#T71W](2026-09-19-cross-provider-qa-a-provider-model-signature-on.md)
(reports under `docs/qa_evidence/2026-09-19-cross-provider-qa/`) established that the question is
open in the literature:

- The crossed **author x reviewer** design is published **exactly once**, at 2x2 — Xiang et al.,
  *"Cross-Model LLM Code Review: Should you use Claude to review Codex or vice versa?"*
  ([2607.21656](https://arxiv.org/abs/2607.21656)), Claude x Codex over 116 tasks. **No published
  cell anywhere has Kimi, GLM, MiniMax or DeepSeek reviewing Claude-authored code** — which is the
  pairing Relay uses most.
- The interaction is real and **flips sign** there: Claude reviewing Codex drafts raises them
  71.6% -> 89.7%; Codex reviewing Claude drafts lowers them. A ranking cannot express that.
- The only proper **reviewer x reviewer** measurement (Kohli,
  [2605.29800](https://arxiv.org/abs/2605.29800), 28 May 2026) is on natural-language inference,
  with a model roster a generation behind, and it says **family explains almost nothing**:
  same-family phi 0.437/0.435 against a cross-family mean of 0.389, across a 0.161-0.603 spread,
  with the three most correlated pairs all cross-family.

So Relay's `VERIFIER_RANK` orders verifiers by a lineage rule that the best available evidence does
not support, for models that evidence never tested. This card measures it for the models Relay
actually routes to, on code.

## The data: the author axis already exists

`mattymchen/codejudgebench` (HuggingFace, Apache-2.0; splits verified live 2026-09-19). Each
**split is the author model**; each row is one problem with that author's `pos_response` (correct)
and `neg_response` (wrong), ground truth from LiveCodeBench hidden tests, plus a `difficulty`
field.

| Author split | Items |
|---|---|
| `codegen/claude_4_opus` | 200 |
| `codegen/claude_4_sonnet` | 285 |
| `codegen/claude_3.7_sonnet` | 325 |
| `codegen/gemini_2.5_pro` / `_flash` / `_flash_lite` | 256 / 430 / 389 |
| `codegen/qwen3_235b` | 218 |

A `neg_response` in a Claude split **is a bug Claude made**, already labelled. No generation spend.

## Design

**Pointwise, not pairwise.** Each response is shown alone and the reviewer answers "will this pass
the hidden tests, and if not where is the fault?". Showing both side by side would measure
discrimination; Relay's QA lane shows one patch, so the pointwise form is the one that transfers.
Scoring `pos_response` too gives the **false-approval rate**, which matters more than detection: a
verifier that rejects everything scores well on bugs and is useless.

**Arms.** Reviewers x author splits, every reviewer seeing the same items:

- reviewers: Kimi, GLM (the owner's question), plus Codex and Claude Code for the 4x4 and the
  **same-family diagonal**, which is the self-preference control.
- authors: the three Claude splits (810 items) and Gemini + Qwen as the contrast.

**Controls that decide whether anyone believes it.** Blind the author, so any family effect is
stylistic and not deference to a name. One prompt and one harness for every reviewer, temperature
fixed. Randomise presentation order. Three repeats on a subset to separate reviewer noise from
reviewer ability, which also caps the correlation any of this can show. Record `difficulty` and
response length as covariates.

**Analysis.**
1. Per-item success matrix `S[reviewer][item]`, kept as raw per-item data so anyone can recompute.
2. Pairwise **phi** and **double-fault** between reviewers, overall and per author split — this is
   the item-level pairwise number that does not exist for code today.
3. Crossed model: `logit P(success) = reviewer + author + reviewer x author + difficulty`, the
   interaction term being the whole question. Report whether the best reviewer depends on the
   author.
4. Per-**bug-type** breakdown (borrow the 10 categories from Martian's Code Review Bench, or the 5
   used in [2606.15689](https://arxiv.org/abs/2606.15689)), giving the reviewer detection profile
   `d_B(t)` and the author error profile `e_A(t)`.
5. Test the **factorisation**: does `escape(A,B) = sum_t e_A(t) (1 - d_B(t))` predict the cells? If
   it does, Relay stores two short vectors per family instead of an n x n table, and a new model
   slots in after a handful of items. The residual on the diagonal is then a clean measure of
   self-preference, separated from "this reviewer is weak at the bug types this author writes".

## Cost and power

| Arm | Judge calls |
|---|---|
| The owner's question (Kimi + GLM x 810 Claude items x pos&neg) | ~3,240 |
| 4x4 with the same-family diagonal and the Gemini/Qwen contrast | ~7,700 |

Roughly 2-3k input tokens per call. GLM is a subscription plan here; Kimi is metered; Codex and
Claude Code run under their own subscriptions. **This is the one gate: it spends the owner's
credits, so it is `waiting_on: owner`.**

Power (`docs/qa_evidence/2026-09-19-cross-provider-qa/power.py`): paired McNemar, a 15-point gap
between two reviewers needs ~400-500 items at the phi ~0.44 implied by Kohli's effective-votes
figure, and ~800+ if the two reviewers are tighter than that. 810 Claude-authored items is inside
that, except in the case where the two reviewers are near-identical — which is itself the finding,
and the case where the choice matters least.

## Honest limits, to be stated in any write-up

Competitive-programming solutions, not repository pull requests. Published work already shows
reviewer F1 collapsing from ~0.36 on injected bugs to 0.007-0.066 on real PRs, so **absolute rates
here will not transfer**; the structural question of whether an author x reviewer interaction
exists will. The repo-level follow-up is SWE-bench's `experiments` tree (~80 submissions, author =
submission, same 500 instances, machine ground truth), at much higher reviewer cost and with no bug
taxonomy.

## Tasks
- [ ] Owner's go-ahead for the credit spend, and which arms to run <!-- t:a1 -->
- [ ] Harness: fetch the splits, one prompt, one adapter per reviewer, resumable, raw per-item results to disk <!-- t:a2 -->
- [ ] Run the 2x2 (Kimi, GLM x Claude authors), both `pos` and `neg` <!-- t:a3 -->
- [ ] Repeat subset for the noise floor, and the blinding/order controls <!-- t:a4 -->
- [ ] Analysis: phi and double-fault matrices, the crossed model, per-bug-type profiles <!-- t:a5 -->
- [ ] Test the factorisation and report the residual <!-- t:a6 -->
- [ ] Extend to the 4x4 with the same-family diagonal, if the 2x2 shows anything <!-- t:a7 -->
- [ ] Write the result back to #T71W: keep or change `VERIFIER_RANK`'s ordering, with the reason <!-- t:a8 -->
- [ ] If the finding holds, a write-up — it is new on three axes (reviewer x reviewer on code, the crossed matrix beyond 2x2, the factorisation) <!-- t:a9 -->

## Decisions
- 2026-09-19, owner: "add the pilot as an issue card."
- 2026-09-19, agent: pointwise rather than pairwise review, because Relay's QA lane shows one patch
  at a time and the pairwise form would measure discrimination instead.
- 2026-09-19, agent: `pos_response` is scored as well as `neg_response`, so false approval is
  measured and a reviewer cannot win by rejecting everything.

## Done means
A frozen, reproducible measurement: raw per-item results on disk (one row per reviewer x item x response x repeat, under `docs/qa_evidence/2026-09-25-author-reviewer-matrix/`) for Kimi and GLM over the 810 CodeJudgeBench Claude-authored items, pos and neg, from which anyone can recompute detection and false-approval rates, pairwise phi, the per-bug-type profiles and the crossed reviewer x author model. The owner's question — for Claude-made bugs, is Kimi or GLM the better reviewer — is answered in the report with its uncertainty (McNemar plus the repeat-subset noise floor), and #T71W records whether `VERIFIER_RANK`'s ordering is kept or changed, with the reason (and the table itself edited if it changes). Failure looks like: numbers that cannot be recomputed from the raw data, an answer stated without a noise floor or a false-approval rate, or the ranking left as-is with no recorded reason.

## Plan
**Goal.** Measure the author x reviewer review matrix on CodeJudgeBench and answer the owner's question — for Claude-made bugs, is Kimi or GLM the better reviewer — then keep or change `VERIFIER_RANK`'s ordering with the reason recorded on #T71W. The design and power rationale are on this card and in `docs/qa_evidence/2026-09-19-cross-provider-qa/proposed-study-design.md`. **The 2x2 pilot is 4,040 calls as specified below, including the smoke as part of the main run and two additional repeats on 100 items (three measurements total); 2,020 calls use Kimi. The owner has not approved the current monetary cap. The 4x4 (task a7) needs a second approval.**

**2026-09-26 refresh.** The former call arithmetic was inconsistent: the 80 smoke calls are a
subset of the 3,240 base calls because the run resumes from their saved results; plus 800 repeat
calls = **4,040 total**, of which 1,620 + 400 = **2,020 Kimi calls**. The owner should approve a current
monetary cap rather than an approximate call count. Do not start metered calls from this old
estimate. The frozen dataset and no-spend harness can be prepared first; refresh model IDs,
quota, per-token pricing and the provider adapters immediately before the smoke. Keep the 4x4
expansion separately gated. A ranking change should depend on the paired outcome and false
approval rate, not only a raw detection win.

**Findings (what exists today).**
- `VERIFIER_RANK` (`backend/relay_core/qa_verifiers.py:283`) is the table this card can change: openai > anthropic > glm > kimi > deepseek > gemini > minimax > local, reordered per card by `LINEAGE` in `_rows_for()` nearby. It is data plus comments, not logic; an ordering change is a table edit.
- Reviewer adapters: `scripts/relay-agent.py` shows the funnel — `relay_core.presets.PRESETS`, `relay_core.keystore`, `relay_core.session_protocol.provider_config` — with `kimi` and `glm-coding` keys already stored. The harness wants raw completions (no tools, no conversation), so call the provider funnel directly rather than `Agent`.
- Data: `mattymchen/codejudgebench` (HuggingFace, public, datasets-server API, no auth). Splits are author models; rows carry `question_content`, `starter_code`, `pos_response`, `neg_response`, `difficulty`, `question_id`. Claude splits: 200 + 285 + 325 = 810 items.
- Power: `docs/qa_evidence/2026-09-19-cross-provider-qa/power.py` — 810 items detects a 15-point reviewer gap at phi ~0.44; underpowered only if the two reviewers are near-identical, which is itself the answer.

**Steps.**
1. **Freeze the item set.** `scripts/qa-matrix.py fetch` pulls the three Claude splits and writes `docs/qa_evidence/2026-09-25-author-reviewer-matrix/items.jsonl` (author, question_id, question_content, starter_code, pos, neg, difficulty) plus a SHA of the frozen set. No reviewer spend.
2. **Harness.** Subcommands `fetch`/`run`/`label`/`analyse` in one new `scripts/qa-matrix.py` (stdlib + `relay_core`; no new deps). `run`: one fixed pointwise prompt — task plus one response, author never named, independent single-item call so no order effect, item order randomised in the queue anyway — temperature fixed and recorded, response format `VERDICT: PASS|FAIL` on the first line then the fault location. Raw text and token usage kept per call. Append-only `results/<reviewer>.jsonl`; resume = skip any (reviewer, item, response, repeat) row already present; backoff and re-queue on rate limits; a small worker pool per reviewer (serial calls would take days). Unit tests for the parser and the resume logic (`tests/test_qa_matrix.py`, offline, no network).
3. **Smoke before spend.** 20 items x 2 reviewers x pos+neg = 80 calls. Proceed only if >=95% parse and no author leakage in the prompt; otherwise fix and re-smoke. Log cost so far.
4. **Run the 2x2.** Kimi + GLM x 810 items x pos+neg = 3,240 calls (Kimi ~1,620 metered; GLM on the coding subscription).
5. **Repeats.** Two additional calls on a random 100-item subset, both reviewers, both responses (+800 calls, three measurements total) — the noise floor that caps any correlation this data can show.
6. **Bug-type labels.** One labeller pass with a subscription CLI (Claude Code or Codex — no metered spend) over the 810 `neg_response`s, 10-category taxonomy per the design doc; `label` subcommand, same JSONL discipline. Hand-check 50 for labeller agreement, recorded in the report.
7. **Analyse.** `analyse` subcommand: per-item success matrix; detection and false-approval per reviewer per author split; pairwise phi and double-fault; McNemar on the paired Kimi-vs-GLM difference; crossed logit `reviewer + author + reviewer x author + difficulty` (hand-rolled IRLS to stay stdlib, stated in the report); per-bug-type profiles `d_B(t)`, `e_A(t)`; factorisation `escape(A,B) = sum_t e_A(t)(1 - d_B(t))` with residuals. Every number in the report must be reproducible by re-running `analyse` over the raw JSONL.
8. **Report and route.** `report.md` in the evidence dir: the answer, the uncertainty, the honest limits already on this card (competitive-programming solutions, absolute rates do not transfer; with only Claude authors the interaction is within-family — the cross-family test is the 4x4). Then either keep `VERIFIER_RANK` (comment updated to 'measured, held') or edit the table rows and record the reason as a `decision` comment on #T71W — the write-back task a8, in either case.
9. **Land.** Commit harness, tests and evidence via `scripts/land.py`; if the gzipped raw results exceed ~20MB, commit the manifest, aggregates and checksums and keep the raw JSONL on disk with its SHA recorded. Move the card to needs-verification with the evidence path; `tests_check` on the card first. Task a7 (4x4) stays open pending the owner's go.

**Risks.**
- **The spend is the gate** (this card's one `waiting_on: owner`). The owner must set a current monetary cap before the 4,040-call pilot; if the owner wants the Gemini/Qwen contrast arms now instead of after the 2x2, that is a separate authorisation.
- GLM subscription quota or Kimi rate limits mid-run: resume makes stopping safe; the run may span days, say so rather than re-calling completed items.
- Parse failures or refusals: never silently dropped — counted, reported, raw text kept.
- Reviewers near-identical: underpowered by construction; report 'the choice does not matter' with the phi that shows it, and do not change the table on noise.
- Stylistic tells survive blinding (the familiarity mechanism the design intends) — a limit to state, not to fix.

**Verify.** `tests/test_qa_matrix.py` (parser, resume, no-network) green via `tests_run`; the >=95% smoke gate passed before the full spend; `analyse` re-run reproduces every number in `report.md`; the evidence dir holds items.jsonl + SHA, raw results, analysis outputs and report.md; #T71W carries the keep-or-change decision.
