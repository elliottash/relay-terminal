# Cross-model correlation of review capability: author×author, reviewer×reviewer, and the crossed term

Research date 2026-09-19. Companion to `docs/qa_evidence/2026-09-19-cross-provider-qa/research-cross-model-qa.md`
(CAPA/Goel, Kim et al., Panickssery, Wataoka, Yang 2026, PoLL, Lee et al.). Nothing there is re-derived except
where a new number is added. Every number is attributed; where I had only an abstract, a PDF that would not
decompress, or a search-engine extraction, I say so and quote nothing I could not see.

## Verdict on the four questions

**(a) Is AUTHOR × AUTHOR measured? YES, thoroughly.** Both in aggregate (report 1's Kim/Goel) and now at the
item level for code: Liu et al. 2026 mined 254 SWE-bench submissions (median nesting **0.935**); Sharifloo et al.
found **114 of 865** tasks failed by all six of six different-vendor models; Ron et al. ran the Knight–Leveson
replication on 48 agent implementations × 1M inputs. **What is new versus report 1** is the *code-specific,
per-instance* evidence — report 1 had MMLU/BBH-style co-error rates; this is which SWE-bench and LiveCodeBench
items each model actually fails, and the answer is "almost the same ones, nested by strength".

**(b) Is REVIEWER × REVIEWER measured? BARELY — once for code, properly once for judges.** For code review,
"Bigger Isn't Always Better" (2606.15689) reports item-level overlap for 5 reviewers on 150 samples and, more
usefully, **union-of-two F1 for four pairs — all negative**. For judging generally, Kohli's "Nine Judges, Two
Effective Votes" is the real thing: per-item success matrix, double-fault, Kish n_eff, 9 judges from 7 families.
**No pairwise φ / κ / double-fault matrix across reviewer models on code exists.** This is the least-served of
the three and the owner's instinct is right.

**(c) Is the CROSSED matrix measured? PARTIALLY — exactly once, at 2×2.** Xiang et al. (2607.21656), Claude
Opus 4.7 × Codex GPT-5.5, 116 LiveCodeBench tasks, all six cells. No 3+ model matrix exists. **No published
cell anywhere has Kimi, GLM, MiniMax or DeepSeek reviewing Claude-authored code.**

**(d) Has the FACTORISATION-THROUGH-BUG-TYPE hypothesis been tested? NO.** Both halves exist separately —
ErrorMap/ErrorAtlas gives author-side error-type profiles e_A(t) over 17 categories for 8,383 models;
2606.15689 gives detector-side profiles d_B(t) over 5 categories for 5 reviewers — and low-rank factorisation
of model×task *capability* matrices is well established (PC-1 ≈ 80%; FA yields 8 factors; BenchPress completes
an 83×49 matrix). **Nobody has multiplied the two profiles, predicted an (author, reviewer) escape rate from
them, or measured the residual.** That is the paper.

---

## 1. AUTHOR × AUTHOR — do two models make the same bugs?

| Paper | What it measures | Key result | URL |
|---|---|---|---|
| **Liu et al., Sep 2026, "Coding Agents Have Converged"** | 254 SWE-bench submissions; per-instance resolved sets, nesting, McNemar | Verified: top two each **396/500**; **union 414**; top-ten union **449**. Top ten share **285 successes and 51 failures**, leaving **164** discriminating instances (n_eff ratio **0.33**; **0.07** for the top two). Median **nesting 0.935** vs a score-implied 0.774 — the weaker system's wins are nearly a *subset* of the stronger's. **0 of 29** adjacent top-thirty pairs separate under paired McNemar. Scaffold moves one model **29.8 pp** vs an 8.8 pp spread across the top thirty | https://arxiv.org/abs/2609.17394 |
| Sharifloo et al. 2025 | 6 models (Claude Sonnet-4, DeepSeek-V3, Qwen3-Coder, GPT-4o, Llama-3.3-70B, Mistral) × 865 tasks | **114 tasks failed by all six**: HumanEval 1, MBPP 2, LiveCodeBench 35, BigCodeBench-Hard 76. Solved-by-all 113/164, 318/378, 43/175, 14/148. Single-model failures rare | https://arxiv.org/abs/2511.04355 |
| Ron, Baudry & Monperrus, Jun 2026 | Knight–Leveson replication: 48 agent implementations, 1,000,000 inputs | Substantial common-mode failure crossing agent **and** language boundaries. 3-version voting cut failures **387.44 → 130.99**; ~**11,844** triples had zero observed failures | https://arxiv.org/abs/2606.20158 |
| Nogueira et al., Jul 2026 | 224 problems × 12 models × 5 languages × 3 prompts | 3-version ensembles realise only **0.43** of theoretical independence gain (5-version 0.44); **same-model below 0.30**. Different models are structurally more diverse yet "still fail on the same tests far more often than expected under independence" | https://arxiv.org/abs/2607.02808 |
| Kim et al., ICML 2025 (numbers beyond report 1) | Error consistency over 350+ models | HELM co-error **60% vs 33% chance**; HuggingFace **42.3% vs 12.7%**; **97.5–100% of all pairs** above chance. Firms each using a *different* random LLM still systemically excluded **~20%** of applicants | https://arxiv.org/abs/2506.07962 |
| Knight & Leveson 1986 | 27 independently written programs, 1M tests | Statistically significant **lack** of failure independence — the historical anchor | https://doi.org/10.1109/TSE.1986.6312924 |
| **ErrorMap / ErrorAtlas**, Jan 2026 | Per-model **failure signature** as a distribution over **17 error categories**, across **8,383 models** | Models have "distinct model-specific patterns" — e.g. Gemini 2.0 Flash Lite peaks on incomplete content, Claude 3.5 Haiku on logical-reasoning errors. This is e_A(t), the author-side half of the factorisation | https://arxiv.org/abs/2601.15812 |
| "Assessing the Quality and Security of AI-Generated Code" | Defect **severity** profile by generating model | **~75%** of GPT-4o's bugs are MAJOR; Claude Sonnet 4 and Llama 3.2 90B highest BLOCKER share (**~14%**); OpenCoder-8B **12%** CRITICAL vs 3–5%; Claude 3.7 Sonnet **13%** CRITICAL code smells | https://arxiv.org/abs/2508.14727 |

Author-side profiles differ. Author-side *item-level* failures nonetheless overlap enormously and are nested by
capability. Both are true: models differ in the **mix** of bug types they emit, while failing on the **same
hard items**.

## 2. REVIEWER × REVIEWER — do two models catch the same bugs?

| Paper | What it measures | Key result | URL |
|---|---|---|---|
| **Kohli, May 2026, "Nine Judges, Two Effective Votes"** | Per-item success matrix, **9 frontier judges from 7 families**, 3 NLI datasets (100 human annotations/item) + RewardBench replication. Metrics: φ, Krippendorff's α, Dawid–Skene, **double-fault**, **Kish n_eff**, Condorcet null | **9 judges ≈ 2 effective votes**; ~¾ of nominal independence lost. Panel accuracy **8–22 pp** below the independence ideal. **The best single judge matches or beats the full panel.** Better aggregation closes **≤11%** of the gap even with oracle access. Robust to prompt, temperature, CoT | https://arxiv.org/abs/2605.29800 |
| **"Bigger Isn't Always Better", Jun 2026** | 5 reviewers (Claude Haiku 4.5, Claude Sonnet 4.6, GPT-5.4 mini, MiniMax M2.7, GLM-5 Turbo), 150 samples; item-level overlap **and union-of-two F1** | **55/150 samples all five found something; 19/150 none did** (shared blind spot). Union with a second reviewer **hurt F1 every time**: Haiku alone **0.365**; ∪Sonnet **0.333 (−8.8%)**, ∪GPT-5.4m **0.331 (−9.3%)**, ∪MiniMax **0.325 (−10.9%)**, ∪GLM **0.304 (−16.7%)**. Authors: "The models largely detect the *same* bugs; adding a second model introduces its false positives without meaningfully increasing true positives" | https://arxiv.org/abs/2606.15689 |
| Norman, Rivera & Hughes, Jun 2026, "Reliability without Validity" | Item-level inter-judge agreement at scale | Thesis: judges are reliable (agree with each other) without being valid. **Result tables would not decompress** — no numbers quoted | https://arxiv.org/abs/2606.19544 |
| Kim 2026, "Are Diversity Metrics Measuring Diversity?" | 5 diversity statistics, 31,900 subsets of 30 models (generation, not judging) | Capability explains **98.9%** of strict diversity, **92.2%** disagreement, **85.7%** double-fault, **45.2%** focal diversity. After capability control **only double-fault survives**: partial Spearman **−0.432** (size 3), **−0.380** (size 4). Majority vote beats the best member in **9.98%** of triples | https://arxiv.org/abs/2607.20768 |
| Ali 2026, "Quantifying Diversity of Thought" | Accuracy-adjusted correlation φ_adj, 767,520 inferences | Raw φ predicts lift at **R² ≤ 0.09**; φ_adj at **R² 0.71 / 0.28 / 0.73**. Max lift **+2.7 pp** | https://arxiv.org/abs/2607.17384 |
| Static-analysis prior art | Tool complementarity by fault type | The pre-LLM contrast case: of 112 CVEs, **33 found only by fuzzers, 30 only by static analysers, 10 by both** — genuinely complementary detector profiles. LLM reviewers do **not** behave this way | https://arxiv.org/abs/2505.22052 |

**The reviewer-side profile looks close to rank-1.** 2606.15689's per-category recall table (n=150):

| Category | Haiku 4.5 | Sonnet 4.6 | GPT-5.4m | MiniMax M2.7 | GLM-5 Turbo |
|---|---|---|---|---|---|
| Security | 69.6% | 69.6% | 69.6% | 71.1% | 69.6% |
| Logic | 24.5% | 19.6% | 16.8% | 16.0% | 12.7% |
| Architecture | 33.3% | 27.3% | 13.6% | 26.1% | 17.4% |
| Best Practice | 6.7% | 0.0% | 0.0% | 0.0% | 0.0% |
| Performance | 0.0% | 0.0% | 0.0% | 0.0% | 4.5% |

Security is **flat across all five** (69.6–71.1%); Logic is **monotone in overall reviewer strength**;
Architecture is nearly so. The only non-monotone cells are two near-zero ones (Haiku's 6.7% Best Practice,
GLM's 4.5% Performance). If this table generalises, d_B(t) ≈ (reviewer strength) × (category detectability) —
a rank-1 detector profile, under which **"pick the strongest reviewer" is optimal and lineage is irrelevant**.
This is one 150-sample table from one paper and should not be over-read, but it is the only direct measurement
of reviewer-side profiles across families that I found, and it points against reviewer diversification.

## 3. AUTHOR × REVIEWER — the crossed term

| Paper | What it measures | Key result | URL |
|---|---|---|---|
| **Xiang, Zhang, Zhang & Xu, Jul 2026, "Cross-Model LLM Code Review"** | Full 2×2 writer × reviewer + both solo baselines; 116 hard/medium LiveCodeBench tasks, Claude Opus 4.7 vs Codex GPT-5.5, static review, high reasoning effort, McNemar + Benjamini–Hochberg | See matrix below — a **sign-flipping interaction** | https://arxiv.org/abs/2607.21656 |
| Chen, Wei, Zhu, Feng & Meng, Apr 2025, "Do LLM Evaluators Prefer Themselves for a Reason?" | Full generator × evaluator cross incl. HumanEval; generators Llama 2 / Mistral / Phi / Qwen 2 / Gemma, evaluators those + GPT-4 + Claude | Extraction reports **evaluator rank is not stable across generators** — a genuine interaction. I could not read the per-cell tables, so direction is sourced, magnitudes unverified | https://arxiv.org/abs/2504.03846 |
| Panickssery et al. 2024 (report 1) | Generator × evaluator cross, summarisation | Right design, wrong task | https://arxiv.org/abs/2404.13076 |

**The one published code matrix** (pass rate on 116 tasks, 95% CI in the paper):

| Writer | solo | Claude reviewer | Codex reviewer |
|---|---|---|---|
| Claude Opus 4.7 | .914 [.862, .957] | **.914 (±0 pp)** | **.828 (−8.6 pp, p_BH=.046)** |
| Codex GPT-5.5 | .716 [.629, .793] | **.897 (+18.1 pp, p_BH=.001)** | **.845 (+12.9 pp, p_BH=.022)** |

Fixes/regressions: Codex→Claude **26/5** (net +21); Codex→Codex 21/6 (+15); Claude→Codex **3/13 (net −10)**;
Claude→Claude 3/3 (0). Regression rate: Claude→Codex **.112** vs Claude→Claude **.026**.

Three readings, and the third is uncomfortable for Relay:

1. **The interaction is real and large.** The same reviewer (Codex) is worth **+12.9 pp** on Codex drafts and
   **−8.6 pp** on Claude drafts — a 21.5-point swing keyed on the author. A single verifier ranking cannot
   express that, which is the owner's point, demonstrated.
2. **The authors' explanation is not lineage.** They attribute it to the capability gap (Claude's solo baseline
   is 19.8 pp higher, so a Codex reviewer has "few real catches available"), to Claude spending more first-pass
   compute (86.2s vs 38.5s solo), and to **intervention style** — Codex reviewers "discard the writer's data
   structure and start over", Claude reviewers "keep the writer's interface and repair one local invariant".
3. **Same-model review was not the loser.** Claude→Claude held the baseline exactly and beat Claude→Codex by
   8.6 pp (p_BH=.032); Codex→Codex gained 12.9 pp. Combined with 2606.15689's result that unioning Haiku with
   **same-family Sonnet** degraded F1 *least* (−8.8%) and with **cross-lineage GLM** degraded it *most*
   (−16.7%), the two published code datasets both fail to support a family-skip rule for reviewers.

## 4. Does the matrix factorise through bug type?

The hypothesis: `escape(A,B) = Σ_t e_A(t) · (1 − d_B(t))`, with no irreducible pair term.

| Piece | Status | Evidence | URL |
|---|---|---|---|
| **e_A(t) exists** — author-side error-type profiles | YES | ErrorMap/ErrorAtlas: 17 categories, 8,383 models, "distinct model-specific patterns"; severity profiles by generator | https://arxiv.org/abs/2601.15812 · https://arxiv.org/abs/2508.14727 |
| **d_B(t) exists** — reviewer-side detection profiles by category | YES, thinly | 2606.15689's 5×5 table above; CR-Bench and Martian's 10 category tags are the infrastructure for more | https://arxiv.org/abs/2606.15689 |
| **Low-rank factorisation of model×task matrices** | YES, well established, for *capability* | Ruan et al.: **PC-1 ≈ 80%**, first 3 PCs **96.7%**. "From Benchmarks to Skills": factor analysis of an LLM×benchmark matrix yields **8 latent skills** and an "intrinsically low-rank structure". IRT/tinyBenchmarks: **100 items** reproduce MMLU to **<2%**. BenchPress completes an **83×49** score matrix by low-rank matrix completion. Li, Simchi-Levi & Sun give the tensor-completion theory (Tucker decomposition; theoretical, no empirical rank) | https://arxiv.org/abs/2405.10938 · https://arxiv.org/abs/2507.20208 · https://arxiv.org/abs/2402.14992 · https://arxiv.org/abs/2604.05460 |
| **Anyone multiplying e_A × d_B and measuring the residual** | **NO** | Not found, in any domain | — |
| **Known residuals a factorisation would miss** | YES, two | **Self-preference** depends on pair identity, not bug type (Panickssery: GPT-4 self-recognition 73.5%; Wataoka: it is a low-perplexity/familiarity effect, so it is *family*-scoped). **Intervention style × capability gap** (Xiang): a weak reviewer facing a strong author's code rewrites rather than repairs, giving a 4× regression rate — a pair effect that no per-category detection profile predicts | https://arxiv.org/abs/2404.13076 · https://arxiv.org/abs/2410.21819 · https://arxiv.org/abs/2607.21656 |

**My reading.** The factorisation is a good first-order model and is testable with data Relay would collect
anyway. But it will not be exact, and the two named residuals are exactly the ones Relay's lineage rule is
trying to exploit — so the residual is the interesting quantity, not the fit. A study that reports
"factorised model explains X% of the (A,B) variance; the residual is concentrated in same-family cells" would
settle whether lineage grouping earns its place.

---

## 5. Secondary context (unchanged from the previous pass)

| Topic | Key numbers | URL |
|---|---|---|
| Kleinberg & Raghavan, monoculture theory | Shared algorithm lowers *collective* decision quality even when individually more accurate | https://www.pnas.org/doi/abs/10.1073/pnas.2018340118 |
| Bommasani et al., outcome homogenization | Sharing **training data** reliably worsens homogenization; sharing a **foundation model** gave mixed results — adaptation method dominated. Abstract only | https://arxiv.org/abs/2211.13972 |
| Self-MoA | Self-MoA beats mixed-model MoA by **6.6%** on AlpacaEval 2.0, **3.8%** average | https://arxiv.org/abs/2502.00674 |
| Are More LLM Calls All You Need? | Voting is **non-monotone** in call count | https://arxiv.org/abs/2403.02419 |
| More Agents Is All You Need | Single-model sampling-and-voting scales — no cross-model diversity needed | https://arxiv.org/abs/2402.05120 |
| LLM-Blender | Best single model top-3 on **52.88%**, blended **68.59%** | https://arxiv.org/abs/2306.02561 |
| LLM-TOPLA | Focal-diversity ensemble pruning | https://arxiv.org/abs/2410.03953 |
| Mixture of Complementary Agents | Complementarity-aware greedy beats both accuracy-seeking and diversity-seeking selection; margins not extractable | https://arxiv.org/abs/2605.24048 |
| PoLL (report 1) vs Kohli | PoLL says juries from disjoint families beat one big judge; Kohli says a 7-family 9-judge panel carried 2 votes and the best single judge matched it. **These are in tension** | https://arxiv.org/abs/2404.18796 · https://arxiv.org/abs/2605.29800 |
| Ilić & Gignac | Single ability factor = **65.6%** of variance over **591 models**, mean loading 0.81 | https://arxiv.org/abs/2310.11616 |
| Huh et al., Platonic Representation | Representations converge as models scale | https://arxiv.org/abs/2405.07987 |
| Sun et al., Idiosyncrasies | **97.1%** 5-way authorship accuracy; survives rewriting/translation/summarisation | https://arxiv.org/abs/2502.12150 |
| PhyloLM / LLM DNA | Output-similarity phylogeny over **111+45** and **305** models recovers families training-free | https://arxiv.org/abs/2404.04671 · https://arxiv.org/abs/2509.24496 |
| Subliminal learning | Traits transfer through unrelated data **only when teacher and student share a base model** | https://arxiv.org/abs/2507.14805 |
| Model collapse | Recursive synthetic training collapses distribution tails | https://www.nature.com/articles/s41586-024-07566-y |
| MATS identity leakage (press) | **Kimi K3 said "Claude" 4/10** unprompted; prompted as Claude, **GLM 5.2** uncensored answers **17%→85%**. Researchers: "**not proof of a distillation**" | https://www.theregister.com/ai-and-ml/2026/07/27/impostor-chinese-models-pretend-theyre-claude/5279165 |
| Anthropic/OpenAI distillation claims | Seven China-based labs, >16M exchanges, ~24,000 accounts. **Allegation by an interested party** | https://www.cnbc.com/2026/02/24/anthropic-openai-china-firms-distillation-deepseek.html |
| MedJUDGE scoping review | Across 49 studies, **no study quantified** inter-judge error correlation in multi-judge pipelines — a citable statement of the gap | https://arxiv.org/abs/2604.25933 |

**Could not find:** any peer-reviewed similarity number saying which frontier teacher GLM, Kimi or MiniMax most
resembles. Lee et al. 2501.12619 (report 1) remains the only quantified attempt and covers DeepSeek, Qwen and
GLM but **not** Kimi or MiniMax. No primary citable source for the EQ-Bench "slop profile" clustering, so I
quote nothing from it. CR-Bench's and Ilić Vulićević's per-cell tables would not decompress from their PDFs.

**Public per-item data that would answer (b) and (d) without new inference spend:**
`withmartian/code-review-benchmark` (173 category-tagged golden comments × 19+ tools × 3 judge models — the
single best source, though authors are humans, so it gives reviewer×item, not author-model×reviewer);
`allenai/reward-bench-results` (per-prompt scores on the core set, per the RewardBench 2 paper);
`ScalerLab/JudgeBench` (per-model/per-dataset response JSONs per its README — I did not enumerate the committed
files); `SWE-bench/experiments` (resolved instance ids per submission, already mined by Liu et al. for
generation); `hongcha0/CodeJudgeBench` (release status of per-example predictions unconfirmed).

---

## What this implies for Relay's lineage groups

### Evidence

- **The author×reviewer interaction is real and sign-flipping** (Xiang): the same reviewer is +12.9 pp on its
  own family's drafts and −8.6 pp on the other's.
- **But neither published code dataset supports a family skip.** Claude→Claude held its baseline and beat
  Claude→Codex by 8.6 pp; unioning Haiku with same-family Sonnet cost least (−8.8%) and with cross-lineage GLM
  cost most (−16.7%). What explained the data was the **capability gap** and **rewrite-vs-repair intervention
  style**.
- **Reviewer profiles look close to rank-1**: Security flat at ~70% across five models from four vendors; Logic
  and Architecture monotone in overall strength.
- **A second reviewer costs more than it buys, in the only measurement**: union-of-two F1 was negative in 4/4
  pairs; 19/150 samples were a shared blind spot.
- **Judge panels are near-degenerate**: 9 judges from 7 families ≈ 2 effective votes; best single judge matches
  the panel (Kohli).
- **Capability is near-unidimensional** (PC-1 ≈ 80%, g ≈ 66–85%, 8 latent skills, 100 items ⇒ MMLU ±2%), and
  **code generation overlap at the frontier is severe** (nesting 0.935; 114/865 shared failures).
- **Cross-model diversity does help authors, partially**: 0.43 of theoretical independence gain vs **<0.30**
  same-model (Nogueira). This remains the cleanest number supporting any cross-model rule in code — and note
  it is an **author-side** result, not a reviewer-side one.

### My inference

- **Grouping GLM/Kimi/DeepSeek/MiniMax/Qwen into one lineage is WEAKLY SUPPORTED** — a reasonable prior from
  Lee et al., identity leakage and contemporaneous open-weight MoE training, but no pairwise CAPA, double-fault
  or phylogenetic distance puts these five inside one cluster and the others outside it, and Kimi and MiniMax
  appear in no quantified distillation study.
- **OpenAI/Anthropic/Google are NOT independent at the frontier.** Similarity rises with capability, one factor
  explains most ability, representations converge with scale, top SWE-bench systems nest at 0.935. Relay's
  lineages are *the least-correlated pairs available*. The honest UI claim is "a differently-blind second
  reader", not "an independent check".
- **The strongest lever in the published data is not lineage.** It is (i) never letting a materially weaker
  model overwrite a stronger model's work — Xiang's bad cell has a 4× regression rate — and (ii) preferring
  verifiers that **flag** rather than **rewrite**. I would make both harder constraints than the lineage skip.
- **The prize is small**: +4.5% union from a second frontier system on SWE-bench; majority vote beats the best
  member in ~10% of triples; best measured two-model lift +2.7 pp; union-of-two reviewers was *negative*.

### What Relay should compute from its own verdict log

Compute the **crossed matrix and both marginals**, not a verifier ranking:

- **Author marginal:** per-author-lineage defect rate, broken down by bug type (Relay's own card labels) — this
  is e_A(t).
- **Reviewer marginal:** per-verifier detection rate by bug type on cards where the defect is known — d_B(t).
  Also log **regression rate**: the fraction of verified cards where the verifier's change made things worse.
  That is the channel through which the only published crossed experiment produced a negative cell, and Relay
  can measure it with no ground-truth defect labels at all.
- **Cross term:** `DF(a,v) = P(real defect AND neither author a nor verifier v caught it)`. Double-fault is the
  only classical diversity statistic that survives capability control (Kim 2026, partial Spearman −0.432).
- **The factorisation test:** fit `Σ_t e_a(t)(1 − d_v(t))`, compare to observed DF(a,v), and look at where the
  residual sits. If the residual is concentrated in same-lineage cells, the lineage groups are earning their
  place. If it is concentrated in low-capability-verifier cells, replace the lineage rule with a capability
  floor. **That single plot is the product decision.**

Two requirements or the estimates are worthless: **randomise a slice** (assign a uniformly random eligible
verifier on 10–20% of cards, or DF is confounded by the current ranking), and **log the resolved upstream
model** with version and reasoning effort (relay-free/OpenRouter must resolve first; Xiang shows reasoning
effort is a live variable).

**Sample size** (standard binomial arithmetic, mine, not from any paper). For a double-fault rate near 0.25, a
95% CI of ±0.05 needs **n ≈ 290 labelled cards per (author × verifier) cell**. To compare two cells —
"same-lineage 0.30 vs cross-lineage 0.20" — at 80% power, α=.05: **≈290 per arm, ~580 total**; for the larger
effect Xiang's data suggests (0.30 vs 0.15), **≈120 per arm, ~240 total**. A full 5×5 matrix at that precision
is several thousand labelled cards, i.e. year-scale. Staging: below ~100 labelled verdicts per cell report
nothing and keep the prior ranking; pool first into `same model / same lineage / different lineage`, decidable
in a few hundred cards; split by lineage pair only after that. The marginals are far cheaper than the cross
term — e_A(t) and d_B(t) each pool over the other index, so a 5×5 design that needs thousands of cards for the
cross term needs only **~300 per lineage** for each marginal. If the factorisation holds, Relay never has to
pay for the cross term at all. Note also that Xiang detected an 18.1 pp effect with **116 paired items**: a
paired within-card design (same card, two verifiers) is far more efficient than independent arms and is worth
running on high-value cards.

### Is this a paper?

Yes, and the sharpened framing makes it a better one. Author×author is done. The crossed design exists once, at
2×2, on 116 competitive-programming tasks, static review, correctness only, by authors who explicitly flag
Gemini/DeepSeek/Qwen/Grok as untested. **Reviewer×reviewer on code is essentially unmeasured** — one 150-sample
overlap count and four union-F1 numbers — and **the factorisation-through-bug-type hypothesis has never been
tested in any domain**, despite both marginals being published separately and low-rank factorisation of
capability matrices being standard. A study that (1) crosses 5–6 lineages as authors and reviewers on real
diffs, (2) conditions on bug type, (3) fits the factorised model and (4) reports where the residual lives would
be new on all three axes, with the MedJUDGE review available as a citable statement of the gap.

**One caveat any such paper must handle:** the existing 2×2 points at capability gap and intervention style,
not lineage, and the reviewer-side profile in the only published table looks rank-1. A design that does not
hold reviewer capability roughly fixed across arms will re-measure the capability gap and call it lineage.

## Sources

Crossed design and item-level review correlation:
- https://arxiv.org/abs/2607.21656 — Cross-Model LLM Code Review (the 2×2 matrix)
- https://arxiv.org/abs/2504.03846 — Do LLM Evaluators Prefer Themselves for a Reason?
- https://arxiv.org/abs/2605.29800 — Nine Judges, Two Effective Votes
- https://arxiv.org/abs/2606.15689 — Bigger Isn't Always Better (reviewer overlap, union F1, per-category recall)
- https://arxiv.org/abs/2606.19544 — Reliability without Validity (tables not extractable)
- https://arxiv.org/abs/2604.25933 — MedJUDGE scoping review (statement of the gap)
- https://arxiv.org/abs/2603.11078 — CR-Bench (PDF not extractable)
- https://arxiv.org/abs/2604.23361 — Locally deployed LLMs for bug detection (per-category + McNemar; table not extractable)

Error-type profiles and factorisation:
- https://arxiv.org/abs/2601.15812 — ErrorMap / ErrorAtlas (17 categories, 8,383 models)
- https://arxiv.org/abs/2508.14727 — severity profiles by generating model
- https://arxiv.org/abs/2507.20208 — From Benchmarks to Skills (factor analysis, 8 latent skills)
- https://arxiv.org/abs/2405.10938 — Observational Scaling Laws (PC-1 ≈ 80%)
- https://arxiv.org/abs/2402.14992 — tinyBenchmarks (IRT)
- https://arxiv.org/abs/2604.05460 — LLM Evaluation as Tensor Completion (theory)
- https://arxiv.org/abs/2310.11616 — g factor over 591 models
- https://arxiv.org/abs/2407.13696 — BenchBench

Author-side overlap:
- https://arxiv.org/abs/2609.17394 · https://arxiv.org/abs/2511.04355 · https://arxiv.org/abs/2606.20158 · https://arxiv.org/abs/2607.02808 · https://arxiv.org/abs/2506.07962 · https://arxiv.org/abs/2006.16736 · https://doi.org/10.1109/TSE.1986.6312924 · https://arxiv.org/abs/2505.22052

Ensembles, diversity, selection:
- https://arxiv.org/abs/2402.05120 · https://arxiv.org/abs/2403.02419 · https://arxiv.org/abs/2502.00674 · https://arxiv.org/abs/2306.02561 · https://arxiv.org/abs/2410.03953 · https://arxiv.org/abs/2607.20768 · https://arxiv.org/abs/2607.17384 · https://arxiv.org/abs/2605.24048 · https://arxiv.org/abs/2512.23340 · https://arxiv.org/abs/2404.18796

Monoculture and lineage:
- https://www.pnas.org/doi/abs/10.1073/pnas.2018340118 · https://arxiv.org/abs/2211.13972 · https://arxiv.org/abs/2405.07987 · https://arxiv.org/abs/2502.12150 · https://arxiv.org/abs/2404.04671 · https://arxiv.org/abs/2509.24496 · https://arxiv.org/abs/2506.01631 · https://arxiv.org/abs/2507.14805 · https://www.nature.com/articles/s41586-024-07566-y
- https://www.theregister.com/ai-and-ml/2026/07/27/impostor-chinese-models-pretend-theyre-claude/5279165 (press; "not proof of a distillation")
- https://www.cnbc.com/2026/02/24/anthropic-openai-china-firms-distillation-deepseek.html · https://thehackernews.com/2026/09/anthropic-says-seven-china-based-ai.html (allegations)

Public per-item data:
- https://github.com/withmartian/code-review-benchmark · https://huggingface.co/datasets/allenai/reward-bench-results · https://github.com/ScalerLab/JudgeBench · https://github.com/SWE-bench/experiments · https://github.com/hongcha0/CodeJudgeBench
