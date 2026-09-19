# Cross-provider QA verification: prior art and error-correlation evidence

Research date: 2026-09-19. Every claim below carries a URL in the source list. Where a number came
out of a secondary extraction rather than a table I could read directly, I say so.

## 1. Prior art

**Short answer: the *practice* is widespread and explicitly argued for; the *mechanism Relay wants
is not shipped anywhere I can find.*** Plenty of tools let you point review at a different model.
Plenty of essays say you should. Two things are essentially absent from shipped products:

1. A rule that *keys the reviewer on the author's identity* ("this was written by X, therefore not X").
2. A *ranked fallback chain of verifiers* that is filtered by what is installed / keyed.

The only implementations of (1) I found are individual repos' own workflow config — most directly
`thefrederiksen/devthrottle` PR #2989, which enforces that a `cc-ship` reviewer must differ from the
author's reported model and only relaxes to same-family-different-model on opt-in; and a documented
risk-tiered implementer→reviewer table published by aft-group (LOW: gpt-5.6-sol writes /
claude-sonnet-5 reviews; STANDARD: claude-sonnet-5 writes / gpt-5.3-codex reviews; etc.). That same
article is worth reading before building this, because its conclusion is that cross-family review is
"an operational control without comparative evidence" — nobody has published a controlled same-family
vs cross-family escaped-defect comparison.

Notable specifics:

- **OpenAI Codex is a plain counter-example.** OpenAI's own writeup states: "The Codex 'code
  generator' and 'code reviewer' are the same model. But the training methods used to teach these two
  skills differ." Their separation is *training-objective* separation, not vendor separation. Their
  published numbers: the reviewer flags issues in 36% of Codex-generated PRs; 46% of flagged issues
  produce a code change (53% on human-written code); >80% positive reactions; they deliberately trade
  recall for precision.
- **Claude Code's `/code-review` and `/code-review ultra` (`/ultrareview`)** are multi-agent — a
  cloud fleet of specialist agents, with a separate agent independently reproducing each finding
  before it lands. Diversity is *across agents and prompts*, all inside the Claude family; subagent
  `model:` frontmatter routes among Claude models only.
- **Cursor Bugbot** uses "a combination of frontier and in-house models" and Cursor's own marketing
  makes the different-model argument, but the models are undisclosed and there is no author-keyed rule.
- **GitHub Copilot code review** explicitly *refuses* model selection — you get Lite/Balanced effort
  levels, and GitHub's position is that switching the model would compromise review reliability.
- **Kilo** is the interesting near-miss: it is model-agnostic, it published data that 32.3% of
  attributed June-2026 reviews already used a different model from the one that wrote the code, and
  its open-weight reviewer study (13 reviewer models, 10,643 reviews) found open models led on
  critical findings per review (Kimi K2.7 Code 0.179, Grok 4.5 0.176, Laguna M.1 0.171 — within 5%).
  But its cloud dispatcher does the *opposite* of a family-skip: PR #6033 pins a cheaper **same-vendor**
  model for BYOK reviews, for billing reasons.
- **"Second opinion" MCP servers** are the closest thing to Relay's feature in the wild, and there
  are many (dshills/second-opinion with pluggable OpenAI/Gemini/Ollama/Mistral; lack435/simple-cross-
  model-review for Claude↔Codex; mauricioreiss/second-opinion-mcp returning machine-readable
  PASS/FAIL verdicts from a different provider). All of them are *manually configured direction*
  ("if Claude calls, use Codex"), with no ranking and no automatic family detection.

| Tool | What it does | Different vendor by default? | Configurable? | Ranks verifiers? |
|---|---|---|---|---|
| Claude Code `/code-review`, `/ultrareview` | Cloud fleet of specialist reviewer agents; each finding independently reproduced by another agent | No — all Anthropic | Model field per subagent (Claude models only) | No |
| OpenAI Codex code review | Reviewer trained as a separate skill on the *same* model; navigates repo, runs tests | No — explicitly the same model | No | No |
| Cursor Bugbot | PR bug-finding; frontier + in-house models | De facto (not the agent that wrote the code), but undisclosed and not author-keyed | No model choice | No |
| GitHub Copilot code review | Automatic PR review | No | Effort levels only; model switching unsupported | No |
| CodeRabbit | 7–8 model ensemble per review incl. a judge pass; markets independence from the authoring agent | Independent *product*, but no published author→reviewer family rule | Limited | No |
| Greptile | Repo-graph-indexed agentic review; runs on Claude (Anthropic Agent SDK) | No — single family | No | No |
| Graphite Diamond / Graphite Agent | Codebase-aware reviewer + fix/merge | No | No | No |
| Qodo | Multi-agent review architecture (Qodo 2.0, Feb 2026) | No published rule | Some | No |
| Devin / Google Jules | Generation agents (Jules on Gemini); review is secondary | No | No | No |
| Aider architect/editor | Splits *reasoning* from *diff emission* across two models; o1-preview + DeepSeek/o1-mini hit 85% SOTA on its benchmark | Often cross-vendor in practice, by accident | Fully | Leaderboard ranks pairs as *authors*, not verifiers |
| Cline (Plan/Act), Roo Code / Kilo Code (modes, orchestrator/boomerang) | Per-mode system prompt, tools and model; you can build a read-only reviewer mode | No | Fully — per-mode model | No (Kilo cloud pins *same*-vendor cheap model for BYOK review) |
| "Second opinion" MCP servers (dshills, lack435, mauricioreiss, …) | One agent asks a different provider to review; structured verdicts | Yes, by construction | Manual direction | No |
| `devthrottle` cc-ship PR #2989 | Enforces reviewer model ≠ author model in `.ship.yaml` | Yes (rule) | Explicit model id | No — single configured reviewer, no chain |
| aft-group cross-family table | Risk-tiered implementer→reviewer mapping (LOW/STANDARD/HIGH/CRITICAL) | Yes (policy) | Hand-written | A fixed table, not a ranking; author admits no comparative evidence |
| Karpathy `llm-council` | N models answer via OpenRouter, each **anonymously** ranks the others, a chairman synthesizes | Yes, by construction | Model list | Produces a ranking *per query*, not a verifier policy |
| Mixture-of-Agents (Together) | Proposers from different vendors + aggregator; 65.1% vs GPT-4o's 57.5% on AlpacaEval 2.0 | Yes — diversity is the mechanism | Yes | No |
| Cohere PoLL | Jury of 3 small judges from **disjoint families** instead of one big judge | Yes, deliberately | Yes | No |
| OpenRouter Auto Router (NotDiamond) | Picks a model per prompt by complexity/cost | Routes for capability/cost, **not** diversity | Yes | Ranks by fit, not by dissimilarity to an author |

So Relay's "signature the commit, then recommend a verifier from a ranked, installed-aware,
family-skipping list" appears to be genuinely new as a *product mechanism*, while being a formalisation
of a practice several hundred engineers are already doing by hand.

## 2. Error correlation between models

| Paper | Finding | Number |
|---|---|---|
| Goel et al., "Great Models Think Alike…", ICML 2025 (arXiv 2502.04313) | Introduces **CAPA (κp)**, chance-adjusted probabilistic agreement on *mistakes*: κp = (c_obs − c_exp)/(1 − c_exp). LLM-as-judge scores show affinity bias toward functionally similar models, after controlling for the judged model's capability | Judge score vs. judge–candidate similarity, Pearson **r = 0.84**; partial correlations controlling for accuracy remain significant across all 9 judges (~0.35–0.65). 39 student models, 130+ models on MMLU-Pro/BBH |
| same | Error similarity **rises with capability**: within a capability bucket, average CAPA to models from *other* developers increases as capability increases | Qualitative trend over 130+ open-weight models |
| same | Weak-to-strong gains are larger the *less* similar supervisor and student are | **r = −0.85** (p<0.01); partial r ≈ −0.35 controlling for accuracy gap |
| Kim, Garg, Peng & Garg, "Correlated Errors in LLMs", ICML 2025 (arXiv 2506.07962) | Large-scale measurement over 350+ LLMs on two leaderboards + a resume-screening task; shared architecture and shared provider drive correlation, but **larger and more accurate models have highly correlated errors even across architectures and providers** | When both models err, they agree **~60% of the time** on one leaderboard dataset |
| Panickssery, Bowman & Feng, NeurIPS 2024 (arXiv 2404.13076) | LLM evaluators **recognise** their own text and **prefer** it; self-preference strength is linearly correlated with self-recognition ability, and fine-tuning self-recognition causally increases self-preference | GPT-4 pairwise self-recognition ≈ **73.5%** out of the box; after fine-tuning on 500 examples GPT-3.5 and Llama 2 exceed **90%**; per-example correlation reported in the 0.37–0.82 range (secondary extraction) |
| Wataoka, Takahashi & Ri, "Self-Preference Bias in LLM-as-a-Judge" (arXiv 2410.21819) | The mechanism is **familiarity, not identity**: judges score *low-perplexity* text above what humans give it, whether or not they wrote it | Effect holds "regardless of whether the outputs were self-generated" |
| Yang et al., "Quantifying and Mitigating Self-Preference Bias of LLM Judges" (arXiv 2604.22891, Apr 2026) | 20 mainstream models; **capability is uncorrelated or negatively correlated with low self-preference bias** — a stronger verifier is not a fairer one | Structured multi-dimensional prompting cuts SPB by **31.5%** on average |
| Shi et al., "Judging the Judges" (arXiv 2406.07791, IJCNLP 2025) | Position bias in pairwise/list-wise judging is systematic, judge- and task-dependent, and grows as the quality gap narrows | 15 judges, 22 tasks, ~40 generators, >150k evaluation instances |
| Verga et al., "Replacing Judges with Juries" (Cohere, arXiv 2404.18796) | A **PoLL** of 3 small judges from *disjoint families* beats a single GPT-4 judge, with less intra-model bias | >7× cheaper; correlates better with human judgment across single-hop, multi-hop QA and Chatbot Arena Hard |
| Lee et al., "Quantification of LLM Distillation" (arXiv 2501.12619) | Measures homogenisation via identity-cognition contradictions and multi-granularity response similarity. **DeepSeek, Qwen and GLM show high distillation; Claude, Doubao and Gemini show notably less** | Ranking, not a single scalar |
| Crupi et al., TSE 2025 (arXiv 2507.16587) | LLM-as-judge *for code* works only at the frontier: GPT-4-turbo matches human agreement; ~10B-class models cannot judge | >80% agreement with humans (the human–human level); 1,405 Java + 1,281 Python methods |
| Jiang et al., CodeJudgeBench (arXiv 2507.10535, ACL 2026) | Judging code is fragile: thinking models >> instruct models; purpose-built judge models (Prometheus, AceCodeRM) near random on code; pairwise beats pointwise; order/naming/comment perturbations shift verdicts | 5,352 samples from LiveCodeBench; order swaps shift accuracy >10 points |
| "Bigger Isn't Always Better" (arXiv 2606.15689, Apr 2026) | Direct model-vs-model **code-reviewer** benchmark, 100 mutation-injected bugs + 50 real bug-fix PRs | Combined F1: Haiku 4.5 **0.365**, Sonnet 4.6 0.343, GPT-5.4 mini 0.326, Minimax M2.7 0.322, GLM-5 Turbo 0.310. Real-PR-only F1: 0.066 / 0.050 / 0.038 / **0.007** / **0.008** (−92% to −99% vs synthetic). F1 drops 15× from small to large diffs |
| Martian **Code Review Bench** (codereview.withmartian.com) | Live tool-level leaderboard over 200k+ real PRs, scored by whether developers actually act on a suggestion | Greptile 60.8% F1 (Jul 2026); Qodo claimed 64.3%; CodeRabbit 51.2% (Jan–Feb 2026). Caveat: every vendor benchmarks itself and wins (DeepSource) |
| Kilo open-weight reviewer study | 13 reviewer models, 10,643 reviews; open weights match frontier on critical findings | Kimi K2.7 Code 0.179 critical findings/review, Grok 4.5 0.176, Laguna M.1 0.171; 32.3% of June reviews already used a non-author model |

Two things follow that matter more than the individual numbers.

**Self-preference is a familiarity effect, so it is family-wide, not model-wide.** Wataoka's
perplexity result plus Goel's affinity-bias result together mean the bias attaches to *stylistic and
distributional similarity*, not to a name tag. This is the strongest evidence for Relay keying on
family rather than on exact model id, and it also means **anonymising the implementer in the verifier
prompt is not sufficient** — llm-council anonymises identities, and that helps with explicit
favouritism, but not with the low-perplexity effect. Family-skip is the real control; anonymisation is
a cheap extra.

**"Strongest available verifier" is the wrong objective.** Goel's weak-to-strong result (r = −0.85 on
dissimilarity) and Yang 2026's result (capability uncorrelated or *negatively* correlated with low
self-preference) both point the same way: what you want is complementary error distributions, not the
top of the leaderboard. Cohere's PoLL is the operational version of that finding.

## Recommendation for Relay

*Evidence* supports: skip the author's family; prefer a verifier whose training lineage is unlike the
author's; do not assume the strongest model is the best verifier; log outcomes rather than trusting a
static ranking. *My inference* is everything about the specific family clustering below.

**Cluster the six families by likely shared error distribution, not by vendor name:**

- **A — OpenAI** (codex / gpt-5.x): independent pretraining lineage.
- **B — Anthropic** (claude): independent lineage; measured as *low*-distillation in arXiv 2501.12619.
- **C — Chinese open-weight MoE** (GLM / Kimi / DeepSeek): treat as **one cluster**. Evidence: DeepSeek,
  Qwen and GLM score high on distillation in 2501.12619; press reporting in Feb 2026 describes Anthropic
  accusing Moonshot, DeepSeek and MiniMax of training on Claude outputs (an allegation, secondary
  sourcing — weight it lightly). Inference: they are correlated with each other *and* partially with
  whichever frontier teacher they distilled from. A GLM card verified by Kimi is close to self-review.
- **D — relay-free / OpenRouter**: not a family at all; it is a router. It must be *resolved to the
  concrete upstream model* before the family check runs, or it silently hands a Claude card back to Claude.
- **E — local (bonsai)**: its own lineage, genuinely uncorrelated, but below the capability floor that
  Crupi et al. and CodeJudgeBench both show is needed to judge code. Use as an extra opinion, never sole verifier.

**Proposed default ranking — sort on (1) hard-skip author family, (2) prefer a different *cluster*,
(3) then capability:**

| Card implemented by | Verifier preference order | Notes |
|---|---|---|
| codex (A) | claude → deepseek → glm → kimi → relay-free(non-OpenAI upstream) | Cluster C is partly OpenAI-distilled; independence is weaker than it looks |
| claude (B) | codex → deepseek → glm → kimi → relay-free(non-Anthropic upstream) | codex is the only clean cross-cluster pick; I would not claim a strong ordering inside C here |
| glm (C) | codex → claude → relay-free(A or B upstream) → deepseek/kimi | **Never** verify a C card with another C model by default |
| kimi (C) | codex → claude → relay-free(A or B upstream) → glm/deepseek | same |
| deepseek (C) | codex → claude → relay-free(A or B upstream) → glm/kimi | same |
| relay-free (D) | resolve upstream first, then apply that family's row | Signature must record the upstream, not "relay-free" |
| local bonsai (E) | codex → claude → any C | Uncorrelated but weak; fine as verifier for low-risk cards |

This is the owner's sketch with two changes: **deepseek moves above glm/kimi is not the point — the
point is that glm/kimi/deepseek collapse into one skip-group**, and **relay-free must resolve its
upstream before the family gate**, otherwise the fallback is the one that breaks the property.

Three further recommendations:

1. **Signature granularity: provider + model + version + effort/reasoning setting.** Family alone hides
   a router, and 2606.15689 shows same-family models differ enough as reviewers (Haiku 4.5 beating
   Sonnet 4.6 on F1 and recall at 3.2× lower cost) that the version matters.
2. **Two verifiers for high-risk cards (a PoLL of 2–3 from disjoint clusters)** rather than one
   stronger verifier. That is the one configuration with direct published support.
3. **Make the ranking learned, not fixed.** Martian's approach — score a reviewer by whether its
   findings are *acted on* — is the right local metric, and Switchboard already has the data (which
   verifier's objections led to a change vs. were dismissed). The honest position, per the aft-group
   critique, is that no published study compares same-family to cross-family escaped-defect rates, so
   Relay's own hit-rate log would be the first real evidence either way. Set the default ranking from
   the reasoning above, then let it move.

Finally, a calibration point worth putting in the UI: on *real* PRs, every model in 2606.15689 scored
F1 between 0.007 and 0.066, and diff size cost 15× F1. Cross-family QA buys a *differently-blind*
second reader, not a correctness guarantee — and it buys the most on small diffs.

## Sources

Prior art:
- https://code.claude.com/docs/en/ultrareview — Claude Code `/code-review ultra`
- https://code.claude.com/docs/en/code-review — Claude Code code review
- https://www.infoq.com/news/2026/04/claude-code-review/ — agent-based review, verification pass
- https://alignment.openai.com/scaling-code-verification/ — "the same model", 36%/46%/53% numbers
- https://developers.openai.com/codex/app/review — Codex review docs
- https://openai.com/index/introducing-upgrades-to-codex/ — GPT-5-Codex trained for review
- https://cursor.com/docs/bugbot and https://cursor.com/bugbot — Bugbot
- https://workos.com/blog/cursor-bugbot-autoreview-claude-code-prs — different-model argument in practice
- https://docs.github.com/copilot/using-github-copilot/code-review/using-copilot-code-review
- https://github.blog/changelog/2026-08-07-copilot-code-review-effort-levels-are-generally-available/
- https://github.com/orgs/community/discussions/156147 — Copilot model switching not supported
- https://www.coderabbit.ai/blog/code-review-needs-independence — independence argument, 64.5% self-correction failure
- https://docs.coderabbit.ai/guide/code-review
- https://www.greptile.com/content-library/greptile-martian-code-review-benchmark — 60.8% F1
- https://claude.com/customers/greptile — Greptile on Claude
- https://graphite.com/blog/series-b-diamond-launch and https://graphite.com/blog/introducing-graphite-agent-and-pricing
- https://www.qodo.ai/blog/qodo-ranked-1-ai-code-review-tool-in-martians-code-review-benchmark/
- https://www.coderabbit.ai/blog/coderabbit-tops-martian-code-review-benchmark
- https://aider.chat/2024/09/26/architect.html and https://aider.chat/docs/usage/modes.html
- https://kilo.ai/articles/open-weight-models-code-review — 13 reviewers, 10,643 reviews, 32.3% cross-model
- https://github.com/Kilo-Org/cloud/pull/6033 — same-vendor pinning for BYOK reviews
- https://github.com/lack435/simple-cross-model-review — Claude↔Codex MCP
- https://github.com/dshills/second-opinion — pluggable-provider review MCP
- https://github.com/thefrederiksen/devthrottle/pull/2989 — reviewer-model ≠ author-model rule
- https://dev.to/aft-group/cross-family-code-review-an-operational-control-without-comparative-evidence-1e5
- https://www.augmentcode.com/guides/adversarial-code-review
- https://www.mindstudio.ai/blog/cross-vendor-ai-agent-review-claude-codex
- https://github.com/karpathy/llm-council — anonymised cross-model ranking
- https://arxiv.org/abs/2406.04692 — Mixture-of-Agents (65.1% vs 57.5%)
- https://openrouter.ai/docs/guides/routing/routers/auto-router — capability routing, not diversity
- https://deepsource.com/blog/ai-code-review-benchmarks — vendors benchmark themselves and win

Correlation / judge-bias literature:
- https://arxiv.org/abs/2502.04313 and https://model-similarity.github.io/ — CAPA, affinity bias, r = −0.85
- https://icml.cc/virtual/2025/poster/46528 — ICML 2025 record
- https://arxiv.org/abs/2506.07962 — Correlated Errors in LLMs, 350+ models, 60% co-error agreement
- https://arxiv.org/abs/2404.13076 and https://proceedings.neurips.cc/paper_files/paper/2024/hash/7f1f0218e45f5414c79c0679633e47bc-Abstract-Conference.html — self-recognition / self-preference
- https://arxiv.org/abs/2410.21819 — self-preference as perplexity/familiarity
- https://arxiv.org/abs/2604.22891 — SPB quantification/mitigation, 20 models, 31.5%
- https://arxiv.org/abs/2406.07791 — Judging the Judges (position bias)
- https://aclanthology.org/2025.emnlp-main.86/ — Beyond the Surface: measuring self-preference
- https://arxiv.org/abs/2404.18796 — Replacing Judges with Juries (PoLL)
- https://arxiv.org/abs/2501.12619 — Quantification of LLM Distillation (DeepSeek/Qwen/GLM high; Claude/Gemini low)
- https://arxiv.org/abs/2507.16587 — LLM-as-judge for code generation/summarisation
- https://arxiv.org/abs/2507.10535 — CodeJudgeBench
- https://arxiv.org/abs/2606.15689 — Bigger Isn't Always Better (per-model reviewer F1 table)
- https://codereview.withmartian.com/ — Code Review Bench
- https://dl.acm.org/doi/10.1145/3808144 — SWR-Bench
- https://arxiv.org/abs/2510.24367 — LLM-as-a-Judge for Software Engineering (survey)
- https://www.latent.space/p/ainews-moonshot-kimi-k26-the-worlds — Kimi K2.6 context (distillation allegations, secondary)
