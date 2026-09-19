---
id: T71W
type: work
status: needs-qa-llm
labels: [feature, switchboard, qa]
component: [worker, gui]
milestone: beta
workstream: agent
assignee: agent
implemented_by: anthropic/claude-fable-5-1 (Claude Code; Opus subagents for the code)
rank: zzzzzzy
created: '2026-09-19'
acceptance: a card that lands in the QA lane carries a canonical provider/model signature set by the worker, not typed; its commits carry the same signature as a trailer; the card shows one recommended verifier chosen from a ranked list that skips the implementer's family and anything not installed or keyed; Verify opens a pane on that verifier with the QA brief; and a same-family close is still refused
source: 'owner, in the terminal, 2026-09-19 (pasted brief), plus mid-turn: "i''d like your input on this feature as well, and do research to see if this exists in other harnesses/systems"'
links: {plans: [], commits: [eba4fed, d1d96b7, 2906807, a738b5e, 940438c, 83f2a1f, bc785fc, '0144880', b65d061], evidence: [docs/qa_evidence/2026-09-19-cross-provider-qa/], related: [GT7X, KDK9, XS6Q, VZ69], github: null}
---
# Cross-provider QA: a provider/model signature on every completion and commit, and the Switchboard names the verifier

## Issue
implement cross-provider QA verification in the switchboard. 
commits / completions of an issue get a provider / model signature.
switchboard recommends a model to verify. do research on how errors / capacities are correlated across models, to make a ranking of preferred verifiers. 

eg codex -> claude -> glm -> kimi -> deepseek -> relay

where you skip yourself and otherwise pick the best one available; so for codex, you get claude if its installed, otherwise glm (if installed), etc.

## Owner's asks, 2026-09-19

- The pasted brief above, and mid-turn: *"i'd like your input on this feature as well, and do
  research to see if this exists in other harnesses/systems"*. The research is a separate report
  (`docs/qa_evidence/2026-09-19-cross-provider-qa/research-cross-model-qa.md`) and its findings
  are summarised in "What the ranking rests on" below.

## Assessment (Claude Fable 5.1, 2026-09-19)

**What already exists here.** The QA lane is already cross-family: `board_move_card` refuses to
close a card out of `needs-qa-*` when the closer's model family equals `implemented_by`'s
(`relay_core.board_tools.model_family`, tests in `tests/test_board_tools.py`). What is missing is
everything around that rule:

1. **The signature is free text.** `implemented_by` is whatever the agent typed ("Claude Opus 5
   (pane 2)"). The worker knows its own preset and model exactly, so it should stamp them, and the
   family check should read the *model's* vendor, not the aggregator's (`openrouter/deepseek-…` is
   DeepSeek, not "openrouter"; `codex` is OpenAI; `glm-coding` is GLM).
2. **Commits carry `#ID` but no model.** `git log --grep '#ID'` finds the commits; nothing says who
   wrote them. A trailer on each commit closes that gap without any git hook.
3. **Nobody is told who should verify.** The lane says "a different family"; the card should say
   *which one*, from a ranked list, skipping the implementer and anything that is not actually on
   this machine (no key, no CLI), and offer to open that verifier on the card in one key.

**My view of the ranking.** Two things matter, and they pull in different directions: how good the
verifier is at finding bugs, and how *unlike* the author it is. The second is not a nicety. The
literature (Goel et al. 2025 and the self-preference work; see below) finds that error
correlation between models *rises* with capability, that a judge favours text it could have
written itself, and that distilled models inherit the blind spots of their teacher. So "the
strongest available" is not always the best verifier of a given author: a verifier from a different
lineage catches what a stronger sibling would wave through. The owner's sketch
(codex → claude → glm → kimi → deepseek → relay) is a capability order; the implementation keeps
that as the default preference and adds a *lineage* skip on top, so a card written by a model gets a
verifier from a different lineage first, and a same-lineage model only when nothing else is
installed. Both are data (one table), not code, so the order can be changed when the evidence does.

**Prior art, in one line each** (details in the research report): Claude Code's `/code-review`
and Cursor's Bugbot review with a model of their own vendor; CodeRabbit, Greptile and Copilot let
you pick a model but do not steer you away from the author's family; Aider's architect/editor
split is two models on one task, not an independent check. Nothing I found *recommends* a
cross-vendor verifier from a ranked list against the author's signature. That is the new part.

**Scope kept out of this card.** No automatic verification run (a verifier still has to be opened
and read); no score-keeping of verifier accuracy over time (a later card once verdicts exist); no
change to the human QA lane.

## What the ranking rests on (research, 2026-09-19)

Full report with sources: `docs/qa_evidence/2026-09-19-cross-provider-qa/research-cross-model-qa.md`.

**Prior art.** The practice is common and argued for; the mechanism is not shipped anywhere found.
Claude Code's `/code-review` and `/ultrareview` are many agents, all Anthropic. OpenAI says outright
that Codex's generator and reviewer are the same model, separated by training, not vendor. Cursor's
Bugbot uses undisclosed models with no author-keyed rule; GitHub Copilot review refuses model choice.
CodeRabbit, Greptile, Graphite, Qodo: one family, or an ensemble, with no rule against the author's.
Aider's architect/editor is two models writing, not one checking the other. The closest things are
hand-written repo policies (a `.ship.yaml` that refuses a reviewer equal to the author's model; a
risk-tiered implementer→reviewer table whose author admits no comparative evidence exists), and
"second opinion" MCP servers with a manually configured direction. Kilo measured that 32% of its
attributed reviews already use a different model from the author, and its own cloud pins a *same*
vendor cheaper model for BYOK reviews, for billing. So: ranked, installed-aware, family-skipping
verifier recommendation off a signature is new as a product mechanism.

**Error correlation.** Three findings shape the table:

| Finding | Source | Number |
|---|---|---|
| Judges favour models similar to themselves; error similarity rises with capability | Goel et al., ICML 2025 (2502.04313) | judge score vs similarity r = 0.84; weak-to-strong gain vs dissimilarity r = −0.85 |
| Large accurate models err together even across vendors | Kim et al., ICML 2025 (2506.07962) | when both err they agree ~60% |
| Self-preference is a familiarity effect (low perplexity), so it is family-wide | Panickssery 2024 (2404.13076), Wataoka 2024 (2410.21819) | GPT-4 self-recognition 73.5%; holds for text it did not write |
| A stronger judge is not a fairer one | Yang et al. 2026 (2604.22891) | capability uncorrelated or negatively correlated with low self-preference |
| DeepSeek, Qwen and GLM are high-distillation; Claude and Gemini low | Lee et al. 2025 (2501.12619) | ranking |
| Small models cannot judge code; a jury of disjoint families beats one big judge | Crupi 2025 (2507.16587), CodeJudgeBench (2507.10535), Verga 2024 (2404.18796) | frontier ≥ 80% agreement; PoLL > GPT-4 at 7× less |

**Consequences taken into the design.**

1. **Lineage groups:** `openai`, `anthropic`, `google`, `cn-open` (GLM, Kimi, DeepSeek, MiniMax,
   Qwen), `local`. A GLM card verified by Kimi is close to self-review, so same-lineage verifiers
   go after every other-lineage one, still offered, with the reason on the card.
2. **Relay Free is not a family.** It is judged by the gateway's upstream for the role (today:
   relay-main is GLM-5.3 Flash, relay-flash DeepSeek V4.1 Flash, relay-lite Gemini 3.5 Flash Lite),
   so it cannot hand a GLM card back to GLM unnoticed. Its label names the upstream.
3. **"Strongest available" is the wrong objective.** The order is: not the implementer; a different
   lineage first; then the owner's capability order inside a lineage. Local models are last: own
   lineage, but below the capability floor for judging code.
4. **Calibration for the reader:** on real PRs every model in the one direct reviewer benchmark
   scored F1 between 0.007 and 0.066, and F1 fell 15× from small to large diffs. Cross-family QA buys
   a differently blind second reader, not a guarantee, and it buys the most on small diffs.

**Left for later cards (needs the owner):** two verifiers from disjoint lineages on high-risk cards
(the one configuration with direct published support); learning the ranking from Relay's own
verdict log rather than fixing it (no published study compares same-family with cross-family
escaped-defect rates, so that log would be the first evidence).

## Design

### Signature

- **Form:** `provider/model`, lower case, from the presets registry: `anthropic/claude-opus-5`,
  `openai/gpt-6-astra`, `glm/glm-5.3`, `kimi/kimi-k3`, `deepseek/deepseek-v4.1-flash`,
  `gemini/gemini-3.1-pro-preview`, `relay-free/relay-main`, `local/<model>`. A guest CLI is
  `openai/codex` and `anthropic/claude-code` (its exact model is not observable from outside).
  The `provider` segment is the *model vendor*: an aggregator route is signed by what it routes to.
  Free text after the slug in parentheses is allowed and ignored (`… (pane 2)`).
- **Who writes it:** the worker, from its own config, whenever a pane agent moves a card to
  `in-progress` or into a QA lane; the agent's own `implemented_by` argument is accepted only when
  the card has none and the worker cannot know (a guest writing through the bridge). The family
  check (`model_family`) becomes signature-aware and maps the legacy free-text forms too.
- **Commits:** the Execute brief (`relay::board::executeTask`) asks for a trailer
  `Implemented-By: <signature>` beside `#ID`; the Verify brief asks for `Verified-By: <signature>`
  on the evidence commit. `relay-board.py verifier` and the card's `qa` block read the trailers of
  `links.commits` (and of `git log --grep '#ID'`) and report a commit whose trailer disagrees with
  the card.
- **Close:** moving a card out of a QA lane to `done` stamps `verified_by` (new work-card field)
  with the closer's signature, and refuses as before when the families match.

### The ranking and the recommendation

- `backend/relay_core/qa_verifiers.py` (new, stdlib only): `signature()`, `family()`,
  `lineage()`, the ordered `VERIFIER_RANK` table (family, label, runners), and
  `recommend(implemented_by, installed_guests, keys, hosted_ok)` →
  `{recommended, alternates, skipped, unavailable}`.
- A **runner** is how a verifier is opened here: `guest:codex`, `guest:claude` (on PATH),
  `preset:<id>` (a key is stored, or the preset is hosted/local). One family may have several
  runners; the first available one is used (guest before API key for OpenAI and Anthropic,
  because the guest CLIs are the stronger harness).
- **Skip rules**, in order: the implementer's own family; then families in the implementer's
  lineage group are moved to the end (not removed); then anything with no available runner. What
  is left is the preference order. Every skip is reported with its reason so the card can say
  "Claude skipped: it implemented this" and "Codex: not installed".
- **Default order** (owner's sketch, extended to the presets Relay has):
  openai → anthropic → glm → kimi → deepseek → gemini → minimax → local. Relay Free is never a
  verifier (owner, 2026-09-19): verifying is not available on the free plan.
  Lineage groups and any reordering come from the research report, recorded in the section below,
  and the table carries a comment per row saying why it sits where it does.

### Where it shows

- **Worker → GUI:** `board_card_get` (and the agent's `board_read`) gains a `qa` object on every
  work card that has an `implemented_by`:

  ```json
  "qa": {"implemented_by": "anthropic/claude-opus-5", "implementer_family": "anthropic",
         "recommended": {"family": "openai", "label": "Codex", "runner": "guest:codex",
                         "model": "codex", "why": "first in the ranking that is not the implementer and is installed"},
         "alternates": [{"family": "glm", "label": "GLM-5.3", "runner": "preset:glm-coding", "model": "glm-5.3"}],
         "skipped": [{"family": "anthropic", "why": "implemented this card"}],
         "unavailable": [{"family": "kimi", "why": "no key"}],
         "commits": [{"hash": "1a2b3c4", "trailer": "anthropic/claude-opus-5", "agrees": true}]}
  ```

  Availability is the worker's to compute (`shutil.which` over the guest registry's binaries,
  `keystore.available()`, the hosted preset), never the GUI's.
- **Card detail:** a card in `needs-qa-llm` shows a line under the fields — *"Verify with
  Codex (installed) · then GLM-5.3 · Claude skipped: it implemented this"* — and a **Verify (v)**
  button beside Execute. The button opens a terminal pane beside the board on the recommended
  runner: a guest runner goes through the pane's own harness-or-terminal decision (the wrapped
  guest as the pane's agent when the worker can run it, its TUI with the brief as the first prompt
  only when it cannot; fixed in `b65d061` after the owner saw Verify open the bare Codex CLI); a preset runner creates the pane on that preset (`createPane {preset}`) and
  `startBoardTask`s the brief. The row's key `v` and a shortcut hint (`board.verify`) follow the
  WARP rule; the label carries its key in parentheses like the other buttons (#QG60).
- **The Verify brief** (`relay::board::verifyTask`): read the card, run its `## QA checklist`,
  write evidence under `docs/qa_evidence/<date>-<slug>/` prefixed `qa-`, write `## Verdict`, then
  either `board_move_card` to `done` or back to `in-progress` with the failures on the thread; sign
  the evidence commit `Verified-By:`; never fix the code yourself.
- **Agent tools:** `board_read` returns the same `qa` block, so a pane agent asked "who should
  verify #K7Q2" answers from it; no new tool.
- **Without Relay:** `scripts/relay-board.py verifier <ID> [--json]` prints the same recommendation
  from the same function, with availability from this machine.

## Tasks
- [x] `qa_verifiers.py`: signature, family (vendor of the model, not the aggregator), lineage, `VERIFIER_RANK`, `recommend()`; `model_family` delegates to it; tests <!-- t:a1 -->
- [x] The worker stamps `implemented_by` from its own config on in-progress and on entering a QA lane; `verified_by` field, stamped on close; format doc and `check` <!-- t:a2 -->
- [x] `qa` block on `board_card_get` / `board_read`, with availability from the worker and the commit trailers of `links.commits`; protocol §19 <!-- t:a3 -->
- [x] `relay-board.py verifier <ID>` <!-- t:a4 -->
- [x] Execute brief asks for the `Implemented-By:` trailer; new `verifyTask` brief with `Verified-By:` <!-- t:b1 -->
- [x] Card detail: the recommendation line and **Verify (v)**; opens a guest or a preset pane with the brief; hint `board.verify`; Qt test <!-- t:b2 -->
- [x] Research report filed under `docs/qa_evidence/2026-09-19-cross-provider-qa/` and its lineage groups written into `VERIFIER_RANK` <!-- t:c1 -->
- [x] A derived **Verified** section (done + `verified_by`), the guest's exact model in the signature, and no Relay Free verifier (owner's answers, 2026-09-19) <!-- t:d1 -->
- [x] Land in `needs-qa-llm` with evidence and a `## QA checklist` <!-- t:c2 -->

## Decisions
- 2026-09-19, owner: "where you skip yourself and otherwise pick the best one available; so for codex, you get claude if its installed, otherwise glm (if installed), etc."
- 2026-09-19, owner: "do research on how errors / capacities are correlated across models, to make a ranking of preferred verifiers."
- 2026-09-19, owner, on a same-lineage verifier being the only one available: "yes, offer with warning."
- 2026-09-19, owner, on the guest CLI before an API key for the same family: "yes."
- 2026-09-19, owner, on the ranking being advice and the different-family close staying the only hard rule: "yes."
- 2026-09-19, owner: "i would say, relay free is never used for verifying -- so verifying is not available on the free plan." Relay Free leaves the verifier table; it still resolves through its upstream (GLM today) when it is the *implementer*; a Relay Free closer is refused.
- 2026-09-19, owner, on a guest's signature: "lets try to record the model used." The form becomes `<vendor>/<model> via claude-code` (or `via codex`), falling back to `anthropic/claude-code` / `openai/codex` when the model cannot be observed.
- 2026-09-19, owner: "so we need a Verified section in the switchboard?" Yes: a section derived from `verified_by` (status `done` with a verifier named), between Needs QA and Done. No new status, folder or migration; a card reaches it only by a QA close.

## What landed (2026-09-19)

| Commit | What |
|---|---|
| `2906807` | `backend/relay_core/qa_verifiers.py` (signature, family, lineage, `VERIFIER_RANK`, `recommend`), the worker's stamp on `implemented_by`, `verified_by`, the `qa` block on `board_card_get` / `board_read`, `relay-board.py verifier`, protocol §19.15 and the format doc |
| `a738b5e`, `83f2a1f` | Relay Free never verifies and cannot close a QA card; a guest signs `<vendor>/<model> via claude-code` from the model its harness reports |
| `d1d96b7` | the verify line, **Verify (v)**, `verifyTask`, the `Implemented-By:` bullet in the Execute brief, a pane on the recommended runner |
| `940438c` | the derived **Verified** section, signature labels, the amber note |
| `0144880` | the line takes the worker's `available` word, so a local model does not read "(key)" |
| `b65d061` | Verify opens a guest verifier through the harness, like the model picker, with the TUI only as the fallback |

Implemented by Claude Opus 5 subagents (backend and GUI), orchestrated and reviewed by Claude
Fable 5.1 in Claude Code; research by a third Opus agent. Tests at landing: `tests/test_qa_verifiers.py`
39, `test_board_tools.py` 135, `test_board_protocol.py` 113, Qt `board` suite 54 + 1 slot extended;
full `ctest` 56/57 and Python 3045/3046, the one failure being another session's uncommitted
`todos.py` rename (#SHE3), not this card.

**Left, and why it is not done here.** A guest launched in the terminal and writing through the
bridge (Tier B) has no model the worker can see, so its `implemented_by` is what the brief asks it to
type. The smallest hook is for the guest bridge to put the model id (Claude Code's `SessionStart` /
statusline `model.id`, Codex's `session-configured`) into the pane's `program_state` and for the
worker to pass it to `qa_verifiers.guest_signature`; those files belong to the #GT7X session.
`BOARD.md` has no "verified by" column: the index groups by folder and the Done tab is a filter,
so there is no Done table to put it in.

## Second research pass, 2026-09-19: the ranking table is provisional, and the axis may not be lineage

Owner asked for the item-level question the first pass did not answer: *"what is the cross-model
correlation of review capabilities, at the task level… for a given bug type, is kimi success more
correlated with claude or codex. if that doesnt exist, thats a paper"*, then *"for claude-made bugs,
is kimi or glm better at reviewing them"*, and *"author x author and reviewer x reviewer
correlations are also informative here"*. Two reports:
[`research-verifier-rankings.md`](../../../docs/qa_evidence/2026-09-19-cross-provider-qa/research-verifier-rankings.md),
[`research-error-correlation.md`](../../../docs/qa_evidence/2026-09-19-cross-provider-qa/research-error-correlation.md).
Proposed study and its costing:
[`proposed-study-design.md`](../../../docs/qa_evidence/2026-09-19-cross-provider-qa/proposed-study-design.md).

**The object is a matrix, not a ranking.** `R[author][reviewer]`; this card's rule (one capability
order plus a same-lineage skip) is its rank-1 approximation. The interaction is real and
sign-flipping: Xiang et al., *"Cross-Model LLM Code Review: Should you use Claude to review Codex
or vice versa?"* ([2607.21656](https://arxiv.org/abs/2607.21656), 116 tasks, all four cells)
finds Claude reviewing Codex drafts raises them 71.6% → 89.7%, while Codex reviewing Claude drafts
*lowers* them. A ranking cannot express that.

**Three things now qualify what is written above, and two of them cut against it.**

1. **The lineage axis is weakly supported for code.** Xiang et al. attribute their asymmetry to the
   capability gap and to rewrite-versus-repair style, not to family; and Claude→Claude held its
   baseline and beat Claude→Codex, so same-family review was not the loser. In *"Bigger Isn't
   Always Better"* the union of two **same-family** reviewers cost the least accuracy and a
   **cross-lineage** union the most, and the reviewer-side profile looks rank-1 (Security flat near
   70% across four vendors, Logic and Architecture monotone in overall strength). If that
   generalises, "pick the strongest available" is optimal and lineage is irrelevant.
   The family effect that *is* measured — "Who Judges Matters"
   ([2609.17857](https://arxiv.org/abs/2609.17857)): +6.7 pp family preference beyond
   self-preference, panel composition flipping 18.5% of outcomes — is on general judging, not code.
2. **Two verifiers on high-risk cards is weaker than it looked.** Kohli, *"Nine Judges, Two
   Effective Votes"* ([2605.29800](https://arxiv.org/abs/2605.29800)): nine judges from seven
   families carry about two independent votes, panels land 8–22 pp short of the independent-voting
   ideal, and the best single judge matched the panel. That deferred idea should not be built on
   the assumption that a second verifier adds a second opinion.
3. **Verifier rank really does differ from author rank** (this supports the design): judge ranks
   move up to 14 positions across judging benchmarks, one reward-model leaderboard is led by an 8B
   model, and the capability floor looks like *reasoning*, not size — a thinking 8B beats
   non-thinking 70Bs at judging code, while non-thinking instruct judges sit near chance.

**What is unmeasured, and is therefore the paper.** The crossed author × reviewer design is
published exactly once, at 2×2 (Claude × Codex). **No published cell anywhere has Kimi, GLM,
MiniMax or DeepSeek reviewing Claude-authored code**, so the owner's question is unanswered.
Reviewer × reviewer correlation is measured once for judging and once at n=150 for code review.
Author × author is well measured. And nobody has tested whether the matrix **factorises through
bug type** — `escape(A,B) = Σ_t e_A(t)·(1 − d_B(t))` from an author error profile and a reviewer
detection profile — although both marginals exist separately in the literature. If it factorises,
Relay stores two short vectors per family instead of an n×n table and a new model slots in after a
handful of items; the residual on the diagonal is then a clean measurement of self-preference,
separated from "this reviewer is simply weak at the bug types this author writes".

**Consequence for this card: the order in `VERIFIER_RANK` stays, and its justification is
downgraded from "the research says so" to "a defensible default pending measurement".** The
different-family rule on closing a QA card is unaffected: it is a process-independence guarantee
for the audit trail, which holds whatever the error correlations turn out to be. What is now
marked provisional is only the *lineage ordering* — the claim that a cross-lineage verifier is
better than a stronger same-lineage one.

**The pilot that would settle it, and its cost.** `mattymchen/codejudgebench` (HuggingFace,
Apache-2.0, splits verified live 2026-09-19) is one author model per split, each row carrying that
author's correct and incorrect solution to the same problem with hidden-test ground truth — about
810 Claude-authored items, plus Gemini and Qwen. Reviewing both responses measures detection and
false-approval. The owner's exact question is ~3,240 judge calls on keys this machine already has,
with no generation spend; the 4×4 including the same-family diagonal is ~7,700. Power simulation
([`power.py`](../../../docs/qa_evidence/2026-09-19-cross-provider-qa/power.py)): 810 items gives
0.99 power for a 15-point gap between two moderately independent reviewers, falling to 0.6 if they
share blind spots — and that expensive case is the one where the choice matters least. The items
are competitive-programming solutions, so absolute rates will not transfer to pull requests; the
structural question of whether an author × reviewer interaction exists will. **Not run: it spends
the owner's API credits, and that is his call.**

### The pairwise matrix does exist, and it contradicts the lineage grouping (2026-09-19)

Chasing the owner's question down to item-level *pairwise* numbers found them: Kohli
([2605.29800](https://arxiv.org/abs/2605.29800)) Appendix B publishes the full **9x9 phi matrix of
error correlations between judges**, per item, 9 models from 7 families. The relevant cells:

| Pair | phi | Same family? |
|---|---|---|
| Claude Sonnet 4.5 x Gemini 2.5 Pro | **0.603** | no |
| GPT-4o x Claude Sonnet 4.5 | 0.588 | no |
| Mistral Large 3 x DeepSeek-V3 | 0.564 | no |
| Gemini 2.5 Pro x Llama 4 Scout | **0.161** | no |
| same-family mean (OpenAI / Meta) | 0.437 / 0.435 | yes |
| cross-family mean | 0.389 | no |

**Same family buys +0.047 of correlation, against a spread of 0.161 to 0.603 — and the three most
correlated pairs in the matrix are all cross-family.** The paper's own conclusion is that provider
diversity does not ensure independence. Whatever drives the spread, it is not vendor: the tightest
pair is Anthropic x Google, which this card's `LINEAGE` table calls two independent lines.

The code-side per-category recalls point the same way — Table 6 of
[2606.15689](https://arxiv.org/abs/2606.15689), five reviewers:

| Category | Haiku | Sonnet | GPT-5.4m | MiniMax | GLM |
|---|---|---|---|---|---|
| Security | 69.6 | 69.6 | 69.6 | 71.1 | 69.6 |
| Logic | 24.5 | 19.6 | 16.8 | 16.0 | 12.7 |
| Architecture | 33.3 | 27.3 | 13.6 | 26.1 | 17.4 |
| Best practice | 6.7 | 0.0 | 0.0 | 0.0 | 0.0 |
| Performance | 0.0 | 0.0 | 0.0 | 0.0 | 4.5 |

Security is flat to within 1.5 points across four vendors: on that category a second reviewer is
worth nothing at all. Logic is monotone in overall strength. Only two cells carry any profile
signal — MiniMax beating GPT-5.4 mini on Architecture, and GLM being the only one to see a
Performance issue — so `d_B(t)` is very close to rank-1 plus noise, which is the factorisation
hypothesis answered in the negative for this dataset: there is almost nothing to factorise.

**Recommendation, for the owner (not applied).** The evidence no longer supports reordering by
lineage, and that reordering was mine, added on the first research pass — the owner's original
sketch was a plain preference order with a self-skip ("codex -> claude -> glm -> kimi -> deepseek"),
which is what the newer evidence supports. Suggested: drop the lineage reordering from
`recommend()`, keep `LINEAGE` only to word the warning, and let the order be capability alone.
Unchanged either way: the different-family rule for closing a QA card, which is an
audit-independence guarantee and not a bet on error correlation.

**Provenance and three caveats, because this is load-bearing evidence against a shipped design.**
Kohli was submitted **28 May 2026** (arXiv abstract page, checked directly); "Bigger Isn't Always
Better" is 2026 with an April/June discrepancy between its listed submission date and its arXiv id,
unresolved here. Then:

1. **Not code.** The phi matrix is over three natural-language-inference datasets. The category
   table is code, but n=150.
2. **A model generation behind.** The nine judges in the phi matrix read, on a secondary fetch of
   the paper's HTML, as Claude Sonnet 4.5, Gemini 2.5 Pro, GPT-4o, Mistral Large 3, DeepSeek-V3 and
   Llama 4 Scout — a 2024-25 roster for a mid-2026 paper, and **none of the models Relay actually
   routes to** except DeepSeek. Goel et al. find error correlation *rises* with capability, so the
   +0.047 family effect measured on that roster is not safe to project onto today's frontier in
   either direction.
3. **The cell values are a secondary read.** The 9x9 matrix is in the paper's Appendix B; the
   individual phi values above came from a fetch of the HTML rather than from a table I read whole.
   Before any of this is used to change `VERIFIER_RANK`, the appendix should be read directly.

Which is the strongest argument for the pilot: it would produce this matrix for the models Relay
runs, on code, rather than borrowing one from another domain and an older generation.

## QA checklist

For a verifier outside the Anthropic family (on this machine `relay-board.py verifier T71W` names
Codex). Evidence under `docs/qa_evidence/2026-09-19-cross-provider-qa/`, files prefixed `qa-`.

- [ ] `PYTHONPATH=backend python3 -m unittest tests.test_qa_verifiers tests.test_board_tools tests.test_board_protocol` passes, and `ctest --test-dir build -R '^board$'` passes.
- [ ] `scripts/relay-board.py verifier T71W` recommends a non-Anthropic verifier, lists Claude as skipped with "implemented this card", lists Relay Free as unavailable with "verifying is not available on Relay Free", and prints each commit's `Implemented-By:` trailer with agrees / no trailer. `--json` matches the shape in protocol §19.15.
- [ ] `recommend()` by hand, with availability passed in: implementer `openai/codex` gets Claude when `claude` is installed, else GLM when keyed, else Kimi, else DeepSeek; implementer `glm/glm-5.3` gets Codex or Claude first and Kimi / DeepSeek last with `same_lineage: true` and a `note`; implementer `relay-free/relay-main` is treated as GLM; nothing available gives `recommended: null` and the Relay Free note.
- [ ] Family mapping: `openrouter` + `deepseek/deepseek-v4.1-flash` signs as DeepSeek, not OpenRouter; `Claude Opus 5 (pane 2)` and `anthropic/claude-opus-5` are one family; `anthropic/claude-opus-5 via claude-code` parses.
- [ ] In a scratch board: a pane agent moving a card to `in-progress` gets `implemented_by` stamped from the worker's preset and model whatever it typed; closing from a QA lane with a different family stamps `verified_by`; the same family is refused (`independent_model`); a `relay-free/...` closer is refused.
- [ ] Live under Xvfb (isolated `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`, `TMPDIR` under a short path, `RELAY_KEYRING=off`): a `needs-qa-llm` card shows the verify line and **Verify (v)**; `v` and the click open a pane beside the board on the recommended runner with the brief (a Codex or Claude Code verifier runs through Relay's harness as the pane's agent, not as the bare CLI in the shell, whenever the model picker offers it that way); the thread gets one progress comment and the card does not move; the hint shows once.
- [ ] A `done` card with `verified_by` sits in **Verified** with a `✓ <verifier>` badge, one without stays in Done, and a drag or `Alt+Shift+→` into Verified is refused with the sentence in design §4.11.
- [ ] With no keys and no guest CLI on PATH the line is the amber Relay Free note, Verify is disabled and its tooltip says why.
- [ ] The ranking table's provisional status is visible where it is edited: `VERIFIER_RANK`'s
      comments say the lineage ORDER is a default pending measurement, not a finding, and point at
      the second research pass.
- [ ] The docs say what the code does: `docs/SWITCHBOARD-FORMAT.md` (`verified_by`), protocol §19.15, design §4.10 and §4.11, and the ranking table's row comments cite the research report.
