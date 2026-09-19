---
id: 4QM4
type: work
status: ready
labels: [feature, research, qa, switchboard]
component: [worker]
milestone: beta
workstream: agent
waiting_on: owner
rank: zzzzzzzr
created: '2026-09-19'
acceptance: 'a per-item success matrix for >=2 reviewers over >=2 author models on CodeJudgeBench, with detection and false-approval rates, pairwise phi between reviewers, a per-bug-type breakdown, and a stated answer to whether the best reviewer depends on the author; the result either keeps or changes VERIFIER_RANK''s ordering, with the reason recorded on #T71W'
source: 'owner, in the terminal, 2026-09-19, after the #T71W research passes found the question unanswered in the literature'
links: {plans: [], commits: [], evidence: [], related: [T71W], github: null}
---
# Measure the author x reviewer matrix: for Claude-made bugs, is Kimi or GLM the better reviewer?

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
