# What existing research ranks models as *verifiers*?

Research date 2026-09-19. Companion to `docs/qa_evidence/2026-09-19-cross-provider-qa/research-cross-model-qa.md`,
which covered error-correlation and self-preference. This one covers the ranking question: how good is a
model at *checking* work, as opposed to producing it — and, in §1, whether the data to answer the item-level
question (author × reviewer × bug type) has already been published. Every number below came from the source named beside
it. Where I could not open a source, I say so instead of guessing.

**What I could not open:** the JudgeBench PDF tables (scores below come from the alphaXiv overview of the same
paper, so treat them as secondary), the full-text numbers in "Mind the Gap" (2412.02674) and "Debate Helps
Weak Judges" (2605.27483) — both are scanned-stream PDFs my fetcher could not decompress — and
*Are LLMs reliable code reviewers?* (Springer AUSE 2026, 10.1007/s10515-026-00638-5), which is behind an IdP
redirect. A site returned by search as "LLM Judge Benchmark Leaderboard" turned out to be an agentic-capability
aggregator with no judging task in it; I discarded it rather than cite it. For §1 I could not decompress the
Code Review Agent Benchmark PDF (2603.23448), and I could not confirm per-example multi-model prediction
dumps for LLMBar, RM-Bench, MR-Ben, CriticBench, CriticEval, RealCritic or Judge Arena — absence of
confirmation there is my failure to find, not proof of absence.

---

## 1. Item-level data: is the author × reviewer × item matrix published anywhere?

The owner's question — *for a given bug type, is Kimi's review success more correlated with Claude's or with
Codex's, and for Claude-made bugs is Kimi or GLM the better reviewer* — needs three axes on the same items:
who **authored** the code, who **reviewed** it, and what **kind** of defect it is. My job here is the data
side: which published benchmark releases per-example predictions, which records the generating model per item,
and which carries a bug-type taxonomy.

**Headline: no published dataset has all three axes.** Nothing I found crosses author identity with reviewer
identity over shared items with defect-type labels. The owner is right that it is a paper. But two of the
three axes are free in existing data, and the missing axis is the cheap one to run.

### 1a. Author identity recorded per item

| Dataset | Author axis | Items | Bug-type labels | Per-example predictions published? | Licence / URL |
|---|---|---|---|---|---|
| **SWE-bench `experiments` repo** | **Yes — strongest.** ~80+ submission folders under `evaluation/verified`, each one agent+model (`20250522_sweagent_claude-4-sonnet`, `20240728_sweagent_gpt4o`, `20250226_swerl_llama3_70b`, `20250616_Skywork-SWE-32B`, `20240402_rag_gpt4`, …), all over the **same 500 instances**. `model_name_or_path` is in every prediction line | 500 (Verified); also lite / multimodal / multilingual splits | **No** taxonomy — only repo and year breakdowns | **Yes**: `all_preds.jsonl` (per-instance `model_patch`), `results.json` with resolved/unresolved instance ids, trajectories and execution logs | https://github.com/SWE-bench/experiments (licence not stated on the index page; artifacts live in submitters' own repos) |
| **CodeJudgeBench** (`mattymchen/codejudgebench`) | **Yes, and it is the only judging dataset where the generator is the split.** Splits: `claude_3.7_sonnet` (325), `claude_4_opus` (200), `claude_4_sonnet` (285), `gemini_2.5_pro` (256), `gemini_2.5_flash` (430), `gemini_2.5_flash_lite` (389), `qwen3_235b` (218) | codegen 2.1k, codegen_pass5 1.01k, coderepair 2.41k, testgen 840 | **No** — pos/neg decided by LiveCodeBench hidden tests, no failure-mode taxonomy | Dataset yes (`question_content`, `pos_response`, `neg_response`). Per-example **judge** outputs for the paper's 26 judges: I could not confirm a release | **Apache-2.0** · https://huggingface.co/datasets/mattymchen/codejudgebench |
| **JudgeBench** (`ScalerLab/JudgeBench`) | **Yes** — a `response_model` field: **350 pairs from GPT-4o, 270 from Claude-3.5-Sonnet**, same source questions | 620 pairs, incl. a coding domain | Domain only (knowledge / reasoning / math / coding) | Dataset yes; per-example judge predictions not confirmed released | https://github.com/ScalerLab/JudgeBench · https://huggingface.co/datasets/ScalerLab/JudgeBench |
| **RewardBench 2** | **Yes** — `models` field lists the generating models (20-model pool incl. GPT-4o, Claude 3.5 Sonnet) | best-of-4 over 6 skills | Skill/subset only (Factuality, Precise IF, Math, Safety, …) | **Yes — per-example scores per evaluated reward model** in `allenai/reward-bench-2-results` (`model`, `model_type`, `scores`, `subset`, `num_correct`) | **ODC-BY** · https://huggingface.co/datasets/allenai/reward-bench-2 · .../reward-bench-2-results |
| **LLMVul** (2026) | **Yes** — per-item AI tool attribution: Claude Code 14,221, Copilot 4,952, Claude 807, Gemini 386, Cursor 373, ChatGPT 298 functions; 1,684 LLM-attributed commits, 226 real repos | 21,430 C/C++ functions | **Yes — 17 CWE categories**, per item, three-tool ensemble + human validation (κ=0.79); 1,540 vulnerable (7.2%) | No reviewer axis. Paper gives the aggregate 7.2% but **no per-generator defect rate** | **CC-BY-4.0**, Zenodo 10.5281/zenodo.22668216 · https://arxiv.org/html/2609.10945 |
| **SecurityEval / CyberSecEval 3 / SafeGenBench** | Author axis = whatever you generate with (prompt sets, not fixed authors) | 130 prompts→75 CWEs (121 usable→69) / 3,832 prompts, 50 CWEs, 8 languages / ~500 | **Yes — CWE ids per prompt** | n/a | https://arxiv.org/pdf/2408.16100 (survey of all three) |
| **BIG-Bench Mistake** | **Single author only** (PaLM-2 Unicorn, temp 0) — no author axis | 2,186 traces, 5 tasks | Task type only; `mistake_index` per trace | Dataset yes; no multi-model reviewer dump | **Apache-2.0** · https://github.com/WHGTyen/BIG-Bench-Mistake |

### 1b. Reviewer axis and bug-type taxonomy

| Dataset | Reviewer/verifier per-example outputs | Bug-type / error-category taxonomy | Author recorded |
|---|---|---|---|
| **Martian Code Review Bench** (`withmartian/code-review-benchmark`, **MIT**) | **Yes** — per-PR, per-tool judge verdicts, for **19+ tools** (Claude Code, CodeRabbit, Copilot, Cursor, Gemini, …) plus named judge models (Claude Opus 4.5, GPT-5.2, Claude Sonnet 4.5); prompts and pipeline open | **Yes, and it is the best one available**: severity {low, medium, high, critical} × category {**bug, security, concurrency, data, api, perf, test_gap, doc_defect, style, speculative**} on 173 golden comments over 50 PRs (5 repos) | **No — human-authored real OSS PRs.** This is its one gap for the owner's question | https://github.com/withmartian/code-review-benchmark · https://codereview.withmartian.com/ |
| **"Bigger Isn't Always Better"** (2606.15689) | Points at the same Martian repo for data and outputs | 100 mutation-injected bugs + 50 real PRs; I could **not** confirm a per-mutation type label | Original OSS code, not model-authored | https://arxiv.org/pdf/2606.15689 |
| **RewardBench / RewardBench 2 results** | **Yes** — per-example scores for every evaluated RM. The cleanest existing S[verifier][item] matrix | Subset labels only | Yes (see above) | https://huggingface.co/datasets/allenai/reward-bench-2-results |
| **PRMBench** | Eval covers open PRMs (Qwen-PRM, RLHFlow) and prompted critics (o1-mini, DeepSeek-R1, the latter two on a 394-sample subset); per-example dumps not confirmed | **Yes — multi-dimensional error categories** (simplicity / soundness / sensitivity) over 83,456 step labels, 6,216 problems | Generator not the variable | https://arxiv.org/abs/2501.03124 |
| **ProcessBench** | Harness public; per-model predictions not centrally published | Earliest-error index per item; no error taxonomy | n/a | https://huggingface.co/datasets/Qwen/ProcessBench |
| **SWT-Bench** | **Yes** — public leaderboard with full agent traces, predicted patches per method/setting, harness logs | No | Test-generating agent recorded per submission | https://github.com/logic-star-ai/swt-bench |
| **SWR-Bench** | 1,000 PRs (500 change / 500 clean), ground-truth "change-points", LLM matcher ~90% human agreement; per-model outputs not confirmed released | Change-point, not typed | Human PRs | https://dl.acm.org/doi/10.1145/3808144 |
| **CodeReviewer** | Diff-hunk defect yes/no + comment generation; classic dataset, no multi-model output dump | No | Human | (CodeReviewer, Microsoft) |
| **CRScore** | **2.9k human-annotated review-quality scores released** for machine and GitHub comments | Claims/smells, not bug types | Mixed | https://aclanthology.org/2025.naacl-long.457/ |
| **LLMBar, RM-Bench, MR-Ben, CriticBench, CriticEval, RealCritic, Judge Arena** | Datasets and harnesses public; I could **not** confirm per-example multi-model prediction dumps for any of them, nor a downloadable Judge Arena vote log | RM-Bench has style/domain slices; MR-Ben has error-step + reason; none has a bug taxonomy | No | see source list |

### 1c. Usability ranking for computing item-level cross-model review correlation *today*

| # | Dataset | Why | Missing |
|---|---|---|---|
| 1 | **CodeJudgeBench** (Apache-2.0, ~6.4k items, 7 generator models, ≥3 families) | The author axis is **already crossed** over a shared problem pool, with ground truth from hidden tests. Running any reviewer set over it directly answers "for Claude-made bugs, is X or Y better" | Reviewer inference must be run; no bug-type labels |
| 2 | **SWE-bench `experiments`** (~80 systems × 500 shared instances) | Free author axis at the largest scale available, with ground-truth resolved/unresolved per instance, plus the actual patches and trajectories | Reviewer inference must be run; no bug-type labels; licence per-submitter |
| 3 | **Martian Code Review Bench** (MIT) | The only source with **both** a reviewer axis and a real bug-type taxonomy, with per-PR per-tool verdicts already published | Human authors only, and just 50 PRs / 173 comments — too small for a per-bug-type correlation matrix |
| 4 | **RewardBench 2 + results** (ODC-BY) | A genuine, already-computed per-example verifier × item matrix with the generating model recorded | Not code, and reward models rather than reviewers |
| 5 | **JudgeBench** | Two author families (GPT-4o, Claude-3.5-Sonnet) over the same questions, including a coding domain | Only 620 pairs; judge outputs must be re-run |
| 6 | **LLMVul** (CC-BY-4.0) | 21k functions with **both** the generating tool and a CWE label per item — the best *author-specific defect profile by bug type* that exists | No reviewer axis at all |

### 1d. What could be assembled today, and what must be run fresh

**A 2×2 corner is available now, for the price of judge calls only.** Take CodeJudgeBench `codegen`:
Anthropic-authored splits `claude_4_opus` (200) + `claude_4_sonnet` (285) versus non-Anthropic
`gemini_2.5_pro` (256) + `qwen3_235b` (218), all drawn from the same LiveCodeBench pool with pos/neg fixed by
hidden tests. Run Kimi and GLM as judges over all four splits — ~959 items × 2 reviewers × order-swapped ≈
**3.8k judge calls** — and you have S[reviewer][item] for two reviewers across two author families, which is
exactly the owner's question minus the bug-type conditioning. Add Codex and Claude as reviewers and you have
the 4×4 corner including the self-family diagonal, at ~7.7k calls.

**Scaling it up** means SWE-bench `experiments`: pick four submissions whose underlying model families differ,
take their **unresolved** patches on shared instances as "bugs authored by model M", and run the reviewer
panel. Ground truth is free and the authors are genuinely different families.

**What must be run fresh in every design:**
1. **All reviewer inference.** No benchmark publishes a reviewer × item matrix over model-authored code.
2. **Bug-type labels.** Nothing that has an author axis has a defect taxonomy. The two taxonomies worth
   borrowing are Martian's 10 categories (bug/security/concurrency/data/api/perf/test_gap/doc_defect/style/
   speculative, with 4 severities) and LLMVul's 17 CWEs. Labelling ~1k CodeJudgeBench `neg_response`s against
   Martian's categories is the smallest bridge between the two halves of the owner's question.
3. **A same-family control arm.** None of the above was collected to test family-skip, so the diagonal
   (Claude reviewing Claude) has to be run deliberately, not mined.

## 2. Judge / verifier benchmarks and leaderboards

| Benchmark | What it measures | Key result (numbers) | URL |
|---|---|---|---|
| **JudgeBench** (Tan et al., ICLR 2025) | Pairwise judging of *objective correctness*, 350 hard response pairs (knowledge/reasoning/math/coding), order-swapped | Random = 50%. Vanilla GPT-4o **50.86%**; Arena-Hard judge prompt 56.57%; Claude-3.5-Sonnet **64%**; reasoning judges (o3-mini) up to **80.86%**. Reward models 59–64% despite small bases; most fine-tuned judges below random. States a **strong correlation between solving and judging**, but judges *beat* solvers on math and *underperform* them on **coding** | https://arxiv.org/abs/2410.12784 |
| **"Reliability without Validity"** (2026) | 21 judges / 9 providers / ~541k judgments over MT-Bench, JudgeBench, RewardBench; Cohen's κ, test–retest, position bias | JudgeBench κ: Claude Opus 4.6 **0.875**, Gemini 3.1 Pro 0.841, Claude Sonnet 4.6 0.782, Kimi K2.5 0.720, MiniMax M2.7 0.715, GPT-oss-120B 0.687, Claude Haiku 4.5 0.653, GPT-5.4 0.606. Floor: Mixtral 8x22B 0.271, Llama 3.3 70B 0.283, GPT-4o-mini 0.325. Anthropic cohort mean κ **0.770** with the lowest position bias (0.020); OpenAI flagships mean κ **0.467**. **Judge ranks shift by up to 14 positions across benchmarks** (Llama 3.3 70B: rank 5 → rank 20); only Opus 4.6 and Gemini 3.1 Pro stay top-3 everywhere | https://arxiv.org/html/2606.19544v1 |
| **RewardBench 2** (Ai2, ICLR 2026) | Best-of-4 reward-model accuracy on unseen WildChat prompts; 6 skills | Random 25%; ~20 points harder than RewardBench 1; Pearson **0.87** with downstream Best-of-N. Top: Skywork-Reward-V2-**Llama-3.1-8B 84.1**, LMUnit-qwen2.5-72b 82.1, LMUnit-llama3.1-70b 80.5, PGRM 80.0, Gemini 2.5 Pro 79.5 — an 8B model outranks 70B+ ones | https://arxiv.org/abs/2506.01937 |
| **RM-Bench** (ICLR 2025 Oral) | Reward-model sensitivity to *subtle content* change vs resistance to *style* | ~40 RMs; SOTA average **46.6%**, i.e. *below* the 50% random line once style is varied | https://arxiv.org/abs/2410.16184 |
| **LLMBar** (ICLR 2024) | 419 instruction-following pairs, natural + adversarial | GPT-4 best-prompt **82.8%** on adversarial; ChatGPT/LLaMA-2/Falcon near or below chance | https://arxiv.org/abs/2310.07641 |
| **MT-Bench / Chatbot Arena** (Zheng et al. 2023) | LLM-judge agreement with humans | GPT-4 reaches **>80%** agreement — the human–human level. Also documents position bias (up to **75%** first-slot preference), verbosity bias, self-enhancement bias (**10–25%**) | https://arxiv.org/abs/2306.05685 |
| **ProcessBench** | Locate the *earliest* erroneous step in a math CoT; 3,400 expert-annotated cases | Earliest-error localisation, not outcome scoring | https://www.alphaxiv.org/abs/2412.06559 |
| **PRMBench** | Fine-grained process-reward error detection; 6,216 problems, **83,456** step labels, axes = simplicity/soundness/sensitivity | Complements ProcessBench | https://arxiv.org/abs/2501.03124 |
| **BIG-Bench Mistake** (Tyen et al., ACL 2024 Findings) | Find the first logical mistake in a CoT across 5 tasks | SOTA LLMs "generally struggle … even in highly objective, unambiguous cases"; given the error *location*, correction is robust and lifts downstream accuracy on all 5 tasks | https://aclanthology.org/2024.findings-acl.826/ |
| **MR-Ben / MR-GSM8K** (NeurIPS 2024 / ICLR 2025) | Meta-reasoning: decide correctness, locate first error step, explain it (MR-Score = MCC + ACC_step + ACC_reason) | Process-based benchmark built to de-saturate outcome benchmarks | https://arxiv.org/html/2406.13975v3 |
| **VerifyBench** (2025) | Reference-*based* verification accuracy | Already near-solved: GPT-4o-mini **92.85%**, Qwen3-32B **95.8%**. VerifyBench-Hard is not | https://arxiv.org/abs/2505.15801 |
| **CriticBench** (ACL 2024 Findings) | Generate–Critique–Correct across math/commonsense/symbolic/coding/algorithmic, 15 datasets | GQC abilities are roughly linear in each other; **stronger models are better at critiquing weaker ones** | https://arxiv.org/abs/2402.14809 |
| **CriticEval** (NeurIPS 2024) / **RealCritic** | Critique quality on 9 tasks (feedback + correction dimensions) / effectiveness-driven, closed-loop critique | Reference-based critique scoring is unreliable; RealCritic scores critiques by whether they actually fix the answer | https://github.com/open-compass/CriticEval · https://arxiv.org/abs/2501.14492 |
| **JETTS** (ICML 2025) | Judges as *test-time-scaling* evaluators: reranking, step beam search, critique refinement. 10 judges (7B–70B) × 8 generators | Judges are competitive with outcome RMs at **reranking**, **consistently worse than process RMs** at beam search, and their natural-language critiques are "currently ineffective" at guiding the generator | https://arxiv.org/abs/2504.15253 |
| **LongJudgeBench** (2026) | Judging long-form output (avg >9,000 tokens), 6 datasets | Mean accuracy over 32 model×setting combos **0.5639**; best = Qwen3-Max+ref **0.6744**, DeepSeek-V4-Flash+ref 0.6650, GLM-5.1+ref 0.6496; only 12/32 above 0.60. **GPT-5.2 and GPT-4o-mini underperform their general reputation.** Position inconsistency **10.6%–78.7%** | https://arxiv.org/abs/2606.01629 |
| **CodeJudgeBench** (ACL 2026) | LLM-as-judge for code gen / repair / test gen; 26 judges | Thinking models ≫ instruct models; **Qwen3-8B (thinking) beats non-thinking 70B models**; Gemini-2.5 Pro/Flash consistently best; Claude-3.5 and Prometheus-14B below 60% (random = 50%). All judges unstable to ordering, variable naming, misleading comments | https://aclanthology.org/2026.acl-long.888/ |
| **CodeFuse-CR-Bench** (2025) | End-to-end repo-level code review, 601 instances / 70 Python projects | Comprehensive score: **Gemini 2.5 Pro 52.37%**, Claude-Sonnet-4 47.46, Kimi-K2 46.77, DeepSeek-v3.1 42.51, **GPT-5 41.96**, Qwen3-235B 40.45, GPT-4o 35.47 — yet GPT-5 is *top* on the model-based sub-score (64.80). "No single LLM dominates all aspects of CR" | https://arxiv.org/abs/2509.14856 |
| **SWE-Review** (2026) | Agentic reviewer on real issue-resolution patches: Decision Accuracy (approve/reject vs ground truth) and Resolve-Rate-after-Revision | Claude Opus 4.6 **81.8%** weighted DA. DA degrades with author quality: **89.4%** on weak Qwen3-30B patches → **80.5%** Qwen3-Coder → **75.6%** on high-quality GLM-5 patches. Errors: **167 false approvals vs 105 false rejections**; on the high-quality split **83% of all errors are false approvals**. A distilled **8B** reviewer still reaches **69.1% DA** | https://arxiv.org/pdf/2607.06065 |
| **SWT-Bench** (NeurIPS 2024) | Can agents write a *reproducing test* from an issue? (fail-to-pass) | SWE-Agent 15.9%, SWE-Agent+ **18.5%** fail-to-pass; generated tests **double the precision** of SWE-Agent's patches when used as a filter | https://arxiv.org/abs/2406.12952 |
| **CRScore** (NAACL 2025) | Reference-free code-review-comment quality via claims + smells | **0.54 Spearman** with human judgment, best among open-source metrics; 2.9k human-annotated review scores released | https://aclanthology.org/2025.naacl-long.457/ |
| **AutoMonitor-Bench** (2026) | LLM *monitors* catching misbehaviour; 3,010 paired samples, 22 models; Miss Rate + False Alarm Rate | GPT-5-Mini, Gemini-2.5-Flash, DeepSeek-Reasoner lead. Open-source miss rates span **<0.1 to ~0.7**. Explicit finding: **monitoring ability does not reliably track general capability**; MR and FAR trade off | https://arxiv.org/pdf/2601.05752 |
| **Judge Arena** (Atla, HF) | Crowd-voted blind Elo between judge models, 18 judges, K=32, start 1200 | Methodology source; I did not find a current 2026 leaderboard snapshot I could read | https://huggingface.co/blog/arena-atla |
| Bug-detection studies (Defects4J / BugsInPy) | Can an LLM find a real injected/known bug? | 45 Java (Defects4J) + 50 Python (BugsInPy) bugs: **80% false-positive rate in tier 1** once the model is told the code comes from a buggy benchmark — pure anchoring (non-peer-reviewed repo). Locally deployed LLaMA-3.2 / Mistral on 349 BugsInPy bugs: **43–45%** accuracy | https://github.com/sanjana-ghanta/LLM-Bug-Study · https://arxiv.org/abs/2604.23361 |

**Does verifier rank track generator rank?** The honest answer is *partly, and less than you would expect*.
JudgeBench reports a strong solve↔judge correlation but with a domain sign flip — judges beat solvers on math
and lose to them on **code**. "Reliability without Validity" shows judge ranks moving **up to 14 positions**
between judging benchmarks, and puts OpenAI's flagships (κ 0.467 mean on JudgeBench) well below their
generation reputation. LongJudgeBench says the same about GPT-5.2. CodeFuse-CR-Bench puts GPT-5 fifth of
seven as a reviewer while it was top-tier as a generator. AutoMonitor-Bench states outright that monitoring
does not track capability. RewardBench 2's best entry is an 8B model.

## 3. The generator–verifier gap

| Source | What it measures | Key result | URL |
|---|---|---|---|
| **Variation in Verification** (ICLR 2026) | Verification dynamics across problem difficulty × generator capability × verifier capability; 14 open models 2B–72B + GPT-4o | On the **hardest quartile, verifiers falsely reject 49%** of correct responses; **18%** of those because the verifier itself got the wrong answer. Verification gains **peak at weak-to-medium generators and fall sharply as generator capability rises**. Gemma2-9B closed **75.7%** of its gap to Gemma2-27B using a GPT-4o verifier. Verifier↔generator correlation **r ≥ 0.90** on averaged data | https://yefanzhou.github.io/llm-verify-dynamic/ · https://arxiv.org/abs/2509.17995 |
| **Mind the Gap** (Song et al. 2024) | Formalises the generation–verification gap as the self-improvement metric | The gap scales with model capability (I could not decompress the per-model numbers) | https://arxiv.org/pdf/2412.02674 |
| **Huang et al., "LLMs Cannot Self-Correct Reasoning Yet"** (ICLR 2024) | Intrinsic self-correction without external signal | Performance **degrades** after self-correction in every case tested; Llama-2 drops **25.5 pp**. Prior positive results used oracle stopping labels | https://arxiv.org/abs/2310.01798 |
| **Kambhampati group, self-verification limits** | Whether the complexity-theoretic "verification is easier" argument transfers to LLMs | Argues it may be irrelevant for approximate retrieval; in formal planning the gap can **reverse** | https://arxiv.org/abs/2402.08115 |
| **Let's Verify Step by Step** (Lightman et al.) | Process vs outcome supervision | PRM **78.2%** vs ORM **72.4%** on MATH best-of-N; PRM800K = 800k step labels | https://arxiv.org/abs/2305.20050 |
| **Inference Scaling fLaws** (Stroebl, Kapoor, Narayanan) | Resampling against an imperfect verifier | An imperfect verifier imposes a **hard accuracy ceiling** no compute budget escapes; single-sample accuracy is strongly correlated with false-positive rate on HumanEval/MBPP (limited unit-test coverage) | https://arxiv.org/abs/2411.17501 |
| **Large Language Monkeys** (Brown et al. 2024) | Coverage vs selection | Coverage grows log-linearly over **4 orders of magnitude** of samples, but without an automatic verifier majority voting and reward models **plateau after a few hundred** candidates — the verifier, not the generator, is the bottleneck | https://arxiv.org/abs/2407.21787 |
| **Khan et al., ICML 2024 (debate)** | Persuasive debate → truthful answers | Stronger debaters give **marginally** higher judge accuracy | https://arxiv.org/abs/2402.06782 |
| **Kenton et al., NeurIPS 2024 (scalable oversight)** | Weak judges (Gemma7B → Gemini 1.5) judging strong experts, 3 task families | Debate beats consultancy on **all** tasks, but only beats plain QA where there is **information asymmetry**; the weakest judge gained nothing from debate over answering directly | https://arxiv.org/html/2407.04622 |
| **Prover-Verifier Games** (OpenAI 2024) | Training a strong prover to stay checkable by a much weaker verifier | Optimising for correctness alone makes solutions *illegible* — a "legibility tax"; checkability training transfers from the small verifier to time-limited humans | https://arxiv.org/abs/2407.13692 |

## 4. Can a *weaker* verifier catch a *stronger* author's errors?

| Source | Result | URL |
|---|---|---|
| **Weaver** ("Shrinking the Generation-Verification Gap with Weak Verifiers") | Llama 3.3 70B generator + ensemble of **≤70B** judges/RMs reaches **o3-mini-level 87.7%** average. Distilled into a **400M** cross-encoder; at 8B it is within **1.0%** of the large ensemble — explicit weak-to-strong verification | https://arxiv.org/abs/2506.18203 |
| **SWE-Review** | A distilled **8B** reviewer scores **69.1% DA** against Claude Opus 4.6's 81.8%; mixed-trained small models lift direct resolve rate by up to **10.6 pp** in a self-contained loop | https://arxiv.org/pdf/2607.06065 |
| **Variation in Verification** | Weak verifiers match strong ones **at difficulty extremes and for strong generators** — but in exactly those regimes *neither* delivers much gain. Errors from stronger generators are harder to detect | https://arxiv.org/abs/2509.17995 |
| **SWE-Review (author-quality split)** | Direct measurement of the same effect on code: DA **89.4% → 75.6%** as the author model improves | https://arxiv.org/pdf/2607.06065 |
| **When to Trust the Cheap Check** (2026) | Two-threshold weak→strong cascade: MATH L5 **60% at 2 strong calls** vs oracle 63.5% at 2.8; Sudoku 43.1% at 2.87 calls vs 5.32 (**46% fewer**) | https://arxiv.org/pdf/2602.17633 |
| **Strong-Weak collaboration for repo-level code** | A strong+weak pipeline matches the strong model's issue-fix rate at ~**60% of the cost** | https://arxiv.org/abs/2505.20182 |
| **Trust or Escalate** (ICLR 2025) | Cascaded selective evaluation starting at **Mistral-7B** guarantees **>80% human agreement at ~80% coverage** on a Chatbot Arena subset where GPT-4 alone almost never reaches 80% | https://arxiv.org/abs/2407.18370 |
| **RewardBench 2 / CodeJudgeBench** | An 8B RM tops the RewardBench 2 board; a thinking Qwen3-8B beats non-thinking 70Bs at judging code | https://arxiv.org/abs/2506.01937 · https://aclanthology.org/2026.acl-long.888/ |

## 5. Verifier failure modes beyond self-preference

| Source | Failure mode | Number | URL |
|---|---|---|---|
| **SWE-Review** | **Leniency / false approval on good authors** | 167 FA vs 105 FR overall; **83% of errors are false approvals** on the high-quality author split | https://arxiv.org/pdf/2607.06065 |
| **Uncovering Systematic Failures in Verifying Code vs NL Specs** (2025) | **Over-correction** — asking for reasoning + a fix makes the reviewer condemn correct code | GPT-4o HumanEval conformance **52.4% → 11.0%** (−41.4 pp) with richer prompting; MBPP −32.8 pp; Claude-3.5 78.0% → 67.0%. Baseline range only **52–78%** | https://arxiv.org/html/2508.12358v1 |
| **Reliability without Validity** | Raw agreement overstates skill; position bias | Raw exact match exceeds Cohen's κ by **33.8–41.2 pp** on MT-Bench; position bias spans **0.002–0.192**; verbosity bias small (<0.011); test–retest 0.943 → 0.911 on harder data | https://arxiv.org/html/2606.19544v1 |
| **LongJudgeBench** | Position inconsistency on long outputs | **10.6%–78.7%**; GPT-4o-mini at 78.7% on WP-Bench | https://arxiv.org/abs/2606.01629 |
| **MT-Bench** | Position / verbosity / self-enhancement | Up to **75%** first-slot preference; 10–25% self-enhancement | https://arxiv.org/abs/2306.05685 |
| **CodeJudgeBench** | Brittleness to cosmetics | Order swaps, variable renaming and misleading comments shift verdicts; pointwise ≪ pairwise | https://aclanthology.org/2026.acl-long.888/ |
| **RM-Bench** | Style over substance | SOTA RMs **46.6%** — below random — under style interference | https://arxiv.org/abs/2410.16184 |
| **Bug-study anchoring** | Confident-context sycophancy | **80% false-positive rate** when told the file is from a bug benchmark | https://github.com/sanjana-ghanta/LLM-Bug-Study |
| **SycoBench-600 / PARROT** | Sycophancy under doubt, authority, explicit wrong suggestion; correction selectivity | Benchmarks exist; a sycophantic judge up-scores confident text | https://aclanthology.org/2026.findings-acl.1759/ · https://arxiv.org/pdf/2511.17220 |
| **AutoMonitor-Bench** | Specification gaming is the least detectable class, and fine-tuning on known misbehaviour **does not transfer** to unseen strategies | Miss rates up to ~0.7 | https://arxiv.org/pdf/2601.05752 |
| **Rubber-stamping in agentic review** | Implementation agents attempting to approve their own PRs; "fix the test to match the broken behaviour" | Reported operationally, not measured | https://www.oreilly.com/radar/agentic-code-review/ |

## 6. Choosing or routing a verifier

| Source | What it does | Key result | URL |
|---|---|---|---|
| **Who Judges Matters: Family-Conditioned Preference in LLM-as-Judge Panels** (Sept 2026) | Fully crossed design, *no judge ever sees its own output* — isolates **family** preference from self-preference. Llama 3.1, Qwen 2.5, Gemma 2, Yi 1.5 at two scales | Same-family preference: Gemma **+8.4 pp**, Yi +7.6, Qwen +7.6, Llama +3.4; global **+6.7 pp (95% CI 5.3–8.4)**. Larger than 8 of 9 adjacent leaderboard gaps. Panel composition alone flips **18.5%** of pairwise outcomes. Recommendation: **do not let the candidate's family dominate the judge panel** | https://arxiv.org/html/2609.17857 |
| **Trust or Escalate** (ICLR 2025) | Cascaded selective evaluation cheapest→strongest with calibrated confidence ("Simulated Annotators") | Provable human-agreement guarantee; >80% agreement at ~80% coverage starting from Mistral-7B | https://arxiv.org/abs/2407.18370 |
| **Stopping and Routing LLM Judge Panels** (2026) | Classifies each judge as **copy / complement / specialist** relative to the target; emits a calling policy | Role-routed-stop: Hard GSM8K acc 0.6843 at cost 2.90; MBPP public-overfit 0.9900 at 1.52; LLMBar-7 0.7334 at 3.46. Four regimes: route specialists on adversarial slices, stop early when saturated, drop copies, keep full panels only when the information justifies it | https://arxiv.org/html/2608.19802 |
| **When to Trust the Cheap Check** | Two adaptive thresholds → auto-reject / auto-accept / defer | See §4 | https://arxiv.org/pdf/2602.17633 |
| **Jury-on-Demand** (2025) | Predicts per-instance judge reliability from input features, assembles top-K jury, weighted score | Learned judge router; improved correlation on summarisation and RAG | https://arxiv.org/abs/2512.01786 |
| **Finite-Calibration Regime Map for Judge Panels** | How large a panel you can justify given a calibration budget | Panel size should be chosen under the calibration budget, not maximised | https://arxiv.org/pdf/2606.01034 |
| **Meta-Judging survey** (2026) | Taxonomy of judging-the-judge, alignment training, failure modes | Framework, no rankings | https://arxiv.org/abs/2601.17312 |

---

## What this implies for ranking verifiers in Relay

### EVIDENCE (each claim traces to a number above)

1. **Verifier rank is a genuinely different ordering from author rank.** Judge ranks move up to **14 positions** between judging benchmarks (2606.19544); GPT-5-class models under-rank as judges in three independent 2026 sources (2606.19544 κ 0.467 cohort mean, LongJudgeBench, CodeFuse-CR-Bench 5th of 7); monitoring ability explicitly does not track capability (AutoMonitor-Bench); an 8B reward model leads RewardBench 2. JudgeBench's own analysis says judges *underperform* solvers specifically in **coding**.
2. **Family, not just self, biases a verifier.** 2609.17857 measures a **+6.7 pp** same-family boost with the judge never seeing its own output, and shows panel composition flipping **18.5%** of outcomes. This is direct support for Relay's family-skip rule that the earlier report had to infer from self-preference and distillation work.
3. **The better the author, the worse the verifier does.** SWE-Review: decision accuracy **89.4% → 75.6%** as the author model improves, and on the best author split **83% of reviewer errors are false approvals**. "Variation in Verification" finds the same shape in the general case, with **49%** false rejection on the hardest quartile.
4. **Cheap verifiers are real.** Weaver (≤70B ensemble ≈ o3-mini, 8B within 1.0%), an 8B SWE-Review reviewer at 69.1% DA, Trust-or-Escalate starting at Mistral-7B, and cascades at ~60% of strong-model cost. What fails is not *small* but *non-thinking*: CodeJudgeBench has a thinking 8B beating non-thinking 70Bs, and Claude-3.5-class instruct judges below 60% on code.
5. **A verifier's own prompt can destroy it.** Asking for reasoning *and* a fix drove GPT-4o's conformance judgement from 52.4% to **11.0%** (2508.12358). Telling a model the code is suspect produced an **80%** false-positive rate. Pairwise beats pointwise; order and naming perturbations move verdicts.
6. **Verification ceilings are hard.** An imperfect verifier caps resampling accuracy regardless of budget (2411.17501), and selection saturates after a few hundred candidates without a real verifier (2407.21787). Self-verification alone degrades answers (2310.01798).

7. **The item-level question is unanswered, not merely unsummarised.** Across ~30 benchmarks, none publishes
   a reviewer × item matrix over code whose *author model* is recorded, and none that records the author
   carries a defect-type taxonomy. The two best-equipped datasets each have exactly two of the three axes
   (CodeJudgeBench: author + items; Martian: reviewer + bug type).

### MY INFERENCE (not established by the sources)

- **A defensible verifier order of families as of 2026, for chat-style judging: yes, weakly. For code review: no.** The only chance-corrected, multi-benchmark, current-frontier ranking I found is a single 2026 preprint (2606.19544): **Anthropic (κ 0.770 cohort, position bias 0.020) > Google Gemini 3.1 Pro (0.841, heterogeneous within family) > Kimi K2.5 / MiniMax M2.7 (0.715–0.720) > OpenAI flagships (0.467 cohort) > Llama / Mixtral / small instruct (0.27–0.33)**. I would use that as a *tiebreaker inside* Relay's family-skip, not as the primary sort, because it is one preprint, its tasks are not code review, and CodeFuse-CR-Bench and CodeJudgeBench both put **Gemini** first on code specifically while 2606.19544's code-free benchmarks put Anthropic first. On code the published rank orders disagree with each other; say so in the UI rather than implying a settled ladder.
- **Capability floor for a code verifier.** The evidence brackets it rather than fixing it: below ~60% on CodeJudgeBench is random; 10B-class models cannot judge code at all (Crupi, prior report); locally-run 8B-class general models hit 43–45% on real Python bugs. But a *thinking* 8B works (Qwen3-8B, and the 8B SWE-Review reviewer at 69.1% DA). My read: **the floor is an extended-thinking model, not a parameter count** — a small reasoning model is an acceptable verifier, a large non-thinking one is not. That is a change from the earlier report's "local bonsai is below the floor": it is below the floor *if it cannot think at length*, not because it is local.
- **The default should be two disjoint-family verifiers with an escalation rule, not one strong verifier.** Combining Trust-or-Escalate, the role-routing paper and the 83%-false-approval finding: run a cheap different-family verifier first, escalate to a second disjoint family only when it is *not confident*, and bias the prompt toward **finding** rather than **approving**, since on good authors the dominant error is approval, not rejection.
- **The cheapest real experiment Relay could run is not a Relay experiment at all.** CodeJudgeBench already
  holds Claude-, Gemini- and Qwen-authored code candidates over a shared problem pool with test-derived
  ground truth. ~3.8k judge calls with Kimi and GLM as reviewers would produce the owner's exact 2×2 —
  before Relay ships any ranking, and with no code generation spend. I would do that before hard-coding a
  preference order, and I would label the negatives with Martian's 10-category taxonomy so the answer can be
  read per bug type.
- **Log false approvals separately from false rejections.** Every 2026 code source that split them found the ratio flips with author quality. Relay's hit-rate log is worth little if it collapses the two.

## Sources

Item-level data availability (§1): https://github.com/SWE-bench/experiments · https://huggingface.co/datasets/mattymchen/codejudgebench · https://github.com/ScalerLab/JudgeBench · https://huggingface.co/datasets/ScalerLab/JudgeBench · https://huggingface.co/datasets/allenai/reward-bench-2 · https://huggingface.co/datasets/allenai/reward-bench-2-results · https://arxiv.org/html/2609.10945 (LLMVul, Zenodo 10.5281/zenodo.22668216) · https://arxiv.org/pdf/2408.16100 (SecurityEval / CyberSecEval / SafeGenBench scales) · https://github.com/WHGTyen/BIG-Bench-Mistake · https://github.com/withmartian/code-review-benchmark · https://codereview.withmartian.com/ · https://arxiv.org/pdf/2606.15689 · https://huggingface.co/datasets/Qwen/ProcessBench · https://github.com/QwenLM/ProcessBench · https://github.com/logic-star-ai/swt-bench · https://dl.acm.org/doi/10.1145/3808144 · https://www.swebench.com/SWE-bench/guides/evaluation/ · https://arxiv.org/pdf/2603.23448 (Code Review Agent Benchmark — PDF not extractable)

Judge / verifier benchmarks: https://arxiv.org/abs/2410.12784 · https://arxiv.org/html/2606.19544v1 · https://arxiv.org/abs/2506.01937 · https://huggingface.co/spaces/allenai/reward-bench · https://arxiv.org/abs/2410.16184 · https://arxiv.org/abs/2310.07641 · https://arxiv.org/abs/2306.05685 · https://www.alphaxiv.org/abs/2412.06559 · https://arxiv.org/abs/2501.03124 · https://aclanthology.org/2024.findings-acl.826/ · https://github.com/WHGTyen/BIG-Bench-Mistake · https://arxiv.org/html/2406.13975v3 · https://arxiv.org/pdf/2312.17080 · https://arxiv.org/abs/2505.15801 · https://arxiv.org/abs/2402.14809 · https://arxiv.org/abs/2402.13764 · https://github.com/open-compass/CriticEval · https://arxiv.org/abs/2501.14492 · https://arxiv.org/abs/2504.15253 · https://arxiv.org/abs/2606.01629 · https://aclanthology.org/2026.acl-long.888/ · https://arxiv.org/abs/2509.14856 · https://arxiv.org/pdf/2607.06065 · https://arxiv.org/abs/2406.12952 · https://aclanthology.org/2025.naacl-long.457/ · https://arxiv.org/pdf/2601.05752 · https://huggingface.co/blog/arena-atla · https://layer6ai-labs.github.io/RankJudge/ · https://github.com/sanjana-ghanta/LLM-Bug-Study · https://arxiv.org/abs/2604.23361

Generator–verifier gap: https://arxiv.org/abs/2509.17995 · https://yefanzhou.github.io/llm-verify-dynamic/ · https://arxiv.org/pdf/2412.02674 · https://arxiv.org/abs/2310.01798 · https://arxiv.org/abs/2402.08115 · https://arxiv.org/abs/2305.20050 · https://arxiv.org/abs/2411.17501 · https://arxiv.org/abs/2407.21787 · https://arxiv.org/abs/2402.06782 · https://arxiv.org/html/2407.04622 · https://arxiv.org/abs/2407.13692

Weak verifiers: https://arxiv.org/abs/2506.18203 · https://arxiv.org/pdf/2602.17633 · https://arxiv.org/abs/2505.20182 · https://arxiv.org/abs/2407.18370

Failure modes: https://arxiv.org/html/2508.12358v1 · https://aclanthology.org/2026.findings-acl.1759/ · https://arxiv.org/pdf/2511.17220 · https://arxiv.org/pdf/2606.13685 · https://www.oreilly.com/radar/agentic-code-review/ · https://link.springer.com/article/10.1007/s10515-026-00638-5 (could not open)

Verifier selection / routing: https://arxiv.org/html/2609.17857 · https://arxiv.org/html/2608.19802 · https://arxiv.org/abs/2512.01786 · https://arxiv.org/pdf/2606.01034 · https://arxiv.org/abs/2601.17312 · https://arxiv.org/abs/2404.18796
