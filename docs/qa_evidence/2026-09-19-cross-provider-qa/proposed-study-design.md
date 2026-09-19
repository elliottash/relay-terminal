# Item-level cross-model review correlation — design sketch (draft, pending literature)

## The question, stated so it can be measured
For reviewers m and review items i, S[m][i] in {0,1}. Not "who scores highest" but the
STRUCTURE of S: after conditioning on item difficulty, does reviewer success covary by
vendor/lineage, or only by capability?

Raw agreement is the wrong statistic: it is dominated by item difficulty (everyone
passes the easy ones, everyone fails the hard ones), which makes all pairs look
correlated. Needs either chance-adjustment (CAPA/kappa) or, better, a crossed
random-effects model with an item term.

## Model
  logit P(S_mi = 1) = alpha_m + beta_(bugtype(i)) + gamma_(m, bugtype(i)) - delta_i
  alpha_m   reviewer ability
  delta_i   item difficulty (random effect)
  gamma     reviewer x bug-type interaction  <-- THE PARAMETER OF INTEREST
Then residual correlation between reviewers after removing alpha and delta. If gamma is
~0 for all m, there is one latent review ability and diversity buys nothing; lineage
grouping is unsupported and the ranking should be pure capability order.
If gamma varies by family, diversity buys something and the grouping is justified.

## The novel axis: crossed author x reviewer
Every item authored by model A, reviewed by model B, all A x B pairs including A = B.
Directly measures what Relay's rule assumes:
  - diagonal (self-review) vs off-diagonal  -> self-preference at the FAMILY level
  - within-lineage vs cross-lineage off-diagonal -> whether lineage is the right grouping
Nothing found so far crosses these two axes. That is the paper.

## Items: two strata, because they behave differently
1. Injected faults, clean labels, scalable: mutation operators give a principled
   taxonomy (relational/boundary -> off-by-one, arithmetic, negation/logic inversion,
   null deref, resource leak, concurrency, API misuse). Machine-labelled bug type.
2. Real bug-fix commits (reverse the fix): Defects4J / BugsInPy / SWE-bench instances,
   hand- or CWE-labelled.
Both are required: published work shows reviewer F1 collapses from ~0.36 on injected
bugs to 0.007-0.066 on real PRs, so an injected-only study would mislead badly.
Diff size must be a covariate: reviewer F1 reportedly falls ~15x from small to large diffs.

## Reviewers available on this machine today
kimi (key), glm-coding (key), openrouter (key -> DeepSeek, Gemini, Qwen, MiniMax and
more with one key), Claude Code CLI, Codex CLI. That is >= 6 families spanning the three
lineage groups the ranking assumes.

## Scale
Pilot: ~150 items x 6 reviewers = 900 reviews, enough to see whether gamma is nonzero
at all. Full: ~500 items x 8 reviewers x (author crossing) — power depends on the pilot's
effect size; compute it from the pilot rather than guessing here.

## Controls that decide whether anyone believes it
- Order/position and length bias: randomise, and include clean (unbugged) items to
  measure false-approve rate, not just detection.
- Same prompt, same harness for every reviewer; temperature fixed; n repeats per item
  to separate reviewer noise from reviewer ability (a single sample conflates them).
- Blind the author: the reviewer must not be told who wrote it, so any lineage effect is
  stylistic (the familiarity mechanism), not name-driven deference.

## REVISED CENTRE (owner, 2026-09-19): "for claude-made bugs, is kimi or glm better at reviewing them"

The object is a MATRIX R[author][reviewer], not a ranking. Relay's current rule — one
capability order plus a same-lineage skip — is a rank-1 approximation of that matrix.
The falsifiable claim:

  H0 (matrix collapses): reviewer rank is the same whoever authored the code. Then the
     product rule is right, only capability matters, and lineage grouping buys nothing.
  H1 (interaction):      the best reviewer depends on the author. Then Relay needs a
     matrix, and the interesting cells are exactly the ones the skip rule guesses at.

H1 requires author-specific defect profiles to exist at all. That is a cheap prior check
and should be step 1: do Claude-written and GLM-written bugs differ in KIND, not just rate?

## Ground truth without hand-labelling: use the tests

The hard part is knowing whether a naturally-occurring, author-specific bug is really
there. Two ways that avoid hand-labelling, both giving AUTHENTIC author-made bugs rather
than injected ones (injected bugs are the wrong distribution for this question — they are
nobody's bugs):

A. FRESH GENERATION. Author model writes a solution to a task with hidden tests. Hidden
   tests decide truth: fails => a real bug by that author. Reviewer sees the code and the
   task, never the tests, and answers "will this pass?" plus "where is the fault?".
   Cost: author inference + reviewer inference. Full control of the author axis.

B. PUBLISHED AGENT PATCHES (cheapest, and may need no new code generation at all).
   SWE-bench Verified: 500 fixed instances, many public leaderboard submissions, each one
   a named model/agent, each with its per-instance patch and a resolved/not-resolved
   label from the real test suite. A non-resolving patch IS a bug made by a known author,
   with machine ground truth and no labelling cost. Run the reviewer set over patches
   grouped by author model => the matrix, with only reviewer inference to pay for.
   Caveats to state: not-resolved conflates wrong with incomplete; patch-only review is
   harder than repo-level review; submissions differ in scaffold, so "author" is really
   author+scaffold; and resolved-set composition differs by model, so items must be
   matched (compare reviewers on the SAME patches, and condition on instance difficulty).

Design A gives a clean causal author axis; design B gives scale and costs almost nothing.
Do B first as the pilot; it either shows an interaction or it does not.

## What Relay gets out of it regardless of the paper
The Switchboard's verdict log already records (implemented_by, verified_by, outcome) per
card. That is a sparse, slowly-filling sample of exactly this matrix, from real work. The
card already lists "learn the ranking from the verdict log" as a later decision; this is
what the learning target would be. Sparsity is the problem: cells fill at a few cards a
day, so a prior from the offline study is what makes the online estimate usable early.

## Both marginals matter (owner, 2026-09-19), and they may generate the matrix

Three quantities over the same item set, not one:
  AUTHOR x AUTHOR     do two models make the same bugs?   (shared blind spots in writing)
  REVIEWER x REVIEWER do two models catch the same bugs?  (shared blind spots in checking)
  AUTHOR x REVIEWER   the cross term (the owner's kimi-vs-glm-on-claude-bugs question)

Author x author is the best served by existing data: per-instance pass/fail for many
models on the same benchmark is published everywhere, and it is roughly what the existing
error-correlation literature measures. Reviewer x reviewer looks unmeasured and is what
decides whether a two-reviewer jury buys anything. The cross term is the product question.

### Factorisation hypothesis — the thing worth testing
  e_A(t) = rate at which author A produces bug type t
  d_B(t) = rate at which reviewer B detects bug type t
  predicted escape rate for the pair = sum_t e_A(t) (1 - d_B(t))
If the matrix factorises this way, it is fully determined by two cheap marginal profiles
over a shared bug-type space, there is no irreducible pair interaction, and the pairing
rule becomes: PICK THE REVIEWER WHOSE DETECTION PROFILE BEST COVERS THIS AUTHOR'S ERROR
PROFILE. That is neither "the strongest reviewer" nor "a different lineage", and it is
computable from data Relay could collect.

Residuals from that model are the interesting part: self-preference is precisely a
pair-identity effect that no bug-type factorisation can express, so the residual on the
diagonal (and within lineage) is a direct measurement of it, cleanly separated from
"this reviewer is simply weak at the bug types this author tends to write".

Practical consequence either way:
  factorises      -> Relay stores two small profile vectors per family, not an n x n table,
                     and new models slot in after a handful of items.
  does not        -> Relay needs the pair table, which is n^2 and slow to fill; the
                     verdict log is then the only realistic source and the offline study
                     supplies the prior.

## Power: the answer's cost depends on the answer (simulation, power.py)

Paired design (both reviewers judge the same items), McNemar on discordant pairs,
alpha .05. Detecting one reviewer at .50 against another at .35 on ONE author's bugs:

  items   rho=0.3  rho=0.5  rho=0.7  rho=0.85     rho = how much the two reviewers
    200      0.68     0.52     0.31      0.14           share blind spots
    400      0.94     0.83     0.60      0.30
    800      1.00     0.99     0.89      0.58
   1600      1.00     1.00     1.00      0.90

So the sample size is governed by the very quantity being measured. Two reviewers that
share blind spots (plausible within one lineage) need ~1600 items per author cell for a
15-point difference; more independent ones need ~400. Smaller differences cost more
again: .35 vs .45 at rho=.5 needs ~800.

Consequences for the design:
- Budget per CELL, not per study. A 4-author x 4-reviewer matrix at 400 items is 6,400
  reviews; at 1,600 it is 25,600. Reviewer inference is the whole cost, which is why the
  SWE-bench-patches route (no author inference to pay for) is the right pilot.
- There is a consolation: when rho is high the comparison is expensive AND the choice
  matters less, since the two reviewers are catching the same things. The expensive case
  is the uninteresting one.
- Run the marginals first. Reviewer x reviewer rho is estimable from the same runs and
  tells you what every other cell will cost before you commit to the full matrix.
- Repeat sampling: a single sample per (reviewer, item) conflates reviewer noise with
  reviewer ability. 3 repeats on a subset estimates the noise floor and puts a ceiling
  on the correlation any of this can show.

## VERIFIED 2026-09-19: the author axis already exists, off the shelf

`mattymchen/codejudgebench` (HuggingFace, Apache-2.0). Splits ARE the author model —
confirmed live against the datasets-server API:
  codegen/claude_4_opus, claude_4_sonnet, claude_3.7_sonnet,
          gemini_2.5_pro, gemini_2.5_flash, gemini_2.5_flash_lite, qwen3_235b
  plus coderepair/ and testgen/ configs with three authors each.
Row fields confirmed: question_content, starter_code, POS_RESPONSE, NEG_RESPONSE,
difficulty, platform, question_id.

`neg_response` is a wrong solution BY THAT AUTHOR, ground truth from LiveCodeBench hidden
tests. So each row is literally "a bug made by Claude" paired with "the same author's
correct code" — the author axis and machine ground truth, already built, no generation
spend. `difficulty` is a ready-made covariate.

Counts (reported by the research agent, from the dataset card): claude_4_opus 200,
claude_4_sonnet 285, claude_3.7_sonnet 325 => ~810 Claude-authored items;
gemini 2.5 pro/flash/lite 256/430/389; qwen3_235b 218.

### The owner's exact question, costed
"For Claude-made bugs, is Kimi or GLM better at reviewing them":
  810 Claude items x 2 responses (neg AND pos, so detection AND false-approve are both
  measured) x 2 reviewers = ~3,240 judge calls. Roughly 2-3k input tokens each.
Power (power.py): 810 items gives ~0.99 for a 15-point gap at rho=.5, ~0.6 at rho=.85.
Enough for a pilot; underpowered only if the two reviewers are near-identical, which is
itself the finding.
Add Codex and Claude Code as reviewers for the 4x4 including the same-family diagonal,
which is the self-preference control: ~7.7k calls.

### Caveat to state loudly in any write-up
These are competitive-programming solutions (Codeforces/LeetCode via LiveCodeBench), not
repository pull requests. Published work already shows reviewer F1 collapsing from ~0.36
on injected bugs to 0.007-0.066 on real PRs, so ABSOLUTE rates here will not transfer.
The STRUCTURAL question — is there an author x reviewer interaction at all — does
transfer, and that is what the pilot is for. SWE-bench `experiments` (~80 submissions,
same 500 instances, author = submission) is the repo-level follow-up, at much higher
reviewer cost and with no bug taxonomy.

## The rho to size against is ~0.44, from the only proper measurement (added 2026-09-19)

The power table above leaves rho free. The one study that measures reviewer x reviewer properly —
Kohli, "Nine Judges, Two Effective Votes" (arXiv 2605.29800), 9 frontier judges from 7 families,
per-item success matrix, Kish n_eff — reports 9 judges carrying about 2 effective votes. Inverting
the Kish formula, n_eff = n / (1 + (n-1) rho):

    rho_bar = (9/2 - 1) / 8 = 0.44        (my inversion, not a number the paper states)

So the mean pairwise correlation between judges is about 0.44, and 78% of nominal independence is
lost. Read against the power table that puts the pilot at ~400-500 items per cell for a 15-point
reviewer gap. CodeJudgeBench has ~810 Claude-authored items, so the pilot is adequately powered
IF review correlation on code resembles correlation on judging. It may not: that inference is the
gap this study exists to close, which is a pleasant circularity — the pilot's first output is the
number that says whether the pilot was big enough.

Caveat on the code-side number, so nobody cites it as more than it is: in "Bigger Isn't Always
Better" (5 reviewers, 150 samples) all five found something on 55 items and none did on 19. Taking
the all-five cell to imply a common rate of 0.82 and assuming independence would predict the
none-found cell at 0.02%, against 12.7% observed — roughly 600x more co-failure. That arithmetic
is mine and it is crude: it assumes one shared rate and ignores item difficulty, which is itself a
large part of any raw co-failure. It illustrates the direction only. Kohli's 0.44 is the
load-bearing figure because the Kish/double-fault machinery adjusts for what a raw count does not.
