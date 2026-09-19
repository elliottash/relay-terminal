---
id: T71W
type: work
status: in-progress
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
links: {plans: [], commits: [], evidence: [], related: [GT7X, KDK9, XS6Q, VZ69], github: null}
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
  runner: a guest runner launches that guest in the pane and gives it the brief as its first
  prompt; a preset runner creates the pane on that preset (`createPane {preset}`) and
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
- [ ] `qa_verifiers.py`: signature, family (vendor of the model, not the aggregator), lineage, `VERIFIER_RANK`, `recommend()`; `model_family` delegates to it; tests <!-- t:a1 -->
- [ ] The worker stamps `implemented_by` from its own config on in-progress and on entering a QA lane; `verified_by` field, stamped on close; format doc and `check` <!-- t:a2 -->
- [ ] `qa` block on `board_card_get` / `board_read`, with availability from the worker and the commit trailers of `links.commits`; protocol §19 <!-- t:a3 -->
- [ ] `relay-board.py verifier <ID>` <!-- t:a4 -->
- [x] Execute brief asks for the `Implemented-By:` trailer; new `verifyTask` brief with `Verified-By:` <!-- t:b1 -->
- [x] Card detail: the recommendation line and **Verify (v)**; opens a guest or a preset pane with the brief; hint `board.verify`; Qt test <!-- t:b2 -->
- [ ] Research report filed under `docs/qa_evidence/2026-09-19-cross-provider-qa/` and its lineage groups written into `VERIFIER_RANK` <!-- t:c1 s=in-progress -->
- [ ] A derived **Verified** section (done + `verified_by`), the guest's exact model in the signature, and no Relay Free verifier (owner's answers, 2026-09-19) <!-- t:d1 -->
- [ ] Land in `needs-qa-llm` with evidence and a `## QA checklist` <!-- t:c2 -->

## Decisions
- 2026-09-19, owner: "where you skip yourself and otherwise pick the best one available; so for codex, you get claude if its installed, otherwise glm (if installed), etc."
- 2026-09-19, owner: "do research on how errors / capacities are correlated across models, to make a ranking of preferred verifiers."
- 2026-09-19, owner, on a same-lineage verifier being the only one available: "yes, offer with warning."
- 2026-09-19, owner, on the guest CLI before an API key for the same family: "yes."
- 2026-09-19, owner, on the ranking being advice and the different-family close staying the only hard rule: "yes."
- 2026-09-19, owner: "i would say, relay free is never used for verifying -- so verifying is not available on the free plan." Relay Free leaves the verifier table; it still resolves through its upstream (GLM today) when it is the *implementer*; a Relay Free closer is refused.
- 2026-09-19, owner, on a guest's signature: "lets try to record the model used." The form becomes `<vendor>/<model> via claude-code` (or `via codex`), falling back to `anthropic/claude-code` / `openai/codex` when the model cannot be observed.
- 2026-09-19, owner: "so we need a Verified section in the switchboard?" Yes: a section derived from `verified_by` (status `done` with a verifier named), between Needs QA and Done. No new status, folder or migration; a card reaches it only by a QA close.
