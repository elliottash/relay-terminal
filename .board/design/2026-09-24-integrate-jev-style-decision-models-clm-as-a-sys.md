---
id: 8VJD
type: work
status: discussing
labels: [feature, agent, qa, research]
waiting_on: owner
priority: -1
rank: zzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
source: Relay conversation, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [1QKM, P7CF, C3Q2, 95VZ], github: null}
---
# Integrate Jev-style decision models (CLM) as a System One layer in Relay

## Issue
research discussion topic: how to integrate jev style decision models, a la CLM, into relay: https://github.com/Contrastive-LM/CLM

put this in a discussing card. its not urgent so i might come back to it later

## Discussion points
First draft, 2026-09-24, from a read of the CLM repository (README, `src/clm/schema.py`, `src/clm/client.py`, `evaluation/bon_eval.py`, `docs/FINETUNING.md`) and Relay's decision seams. Nothing here is approved scope. Not urgent (owner).

### 1. What a CLM-style model is, in Relay terms

CLM (Contrastive Language Models, Kwok et al. 2026, Apache 2.0 code and weights) is a **scorer, not a generator**. Input: a state string and one or more typed questions. Output: a probability distribution. Three question shapes: a yes/no probability ("Noul"), a choice over named options with per-option probabilities and a confidence, and a score on an ordered scale (expected level index plus the distribution). A separate `/v1/rank` endpoint orders candidate answers against a context. Architecture: frozen Qwen3-8B encoder, last-token pooling, a ~20M-parameter projection head per side, bidirectional InfoNCE so states embed near their correct actions. States and actions embed **separately**, so a fixed action set is embedded once and cached; each decision is then one state embedding plus dot products. Reported latency under 60 ms on consumer hardware, 4–9× faster than the generative baseline.

**"Jev"** is never defined in the README. It appears only as the baseline CLM matches on accuracy and beats on latency, and which "fails to serve as a verifier" on long-horizon tasks. Reading: the same group's generative decision model, the System Two sibling. So "Jev-style" here means the *interface* (state in, typed decision out) and CLM is the fast contrastive way to serve it. This is an inference, not a sourced fact.

Two facts constrain the design:

- The headline verifier numbers (Terminal-Bench 2.1 87.6%, DeepSWE 81.6%) are **after lightweight fine-tuning** on those tasks. Zero-shot, the README says the generative verifier scores below pass@1 on long-horizon work. Expect to need Relay-tuned heads for anything beyond the simple classifiers.
- Serving needs a GPU for the encoder (vLLM, Qwen3-8B, ~16 GB bf16) plus a CPU/GPU scoring head. The owner's DGX Spark (`docs/LOCAL-MODELS.md`) can host it; a typical BYOK user cannot, so **every consumer needs a generative fallback**. Whether a hosted CLM endpoint exists is unverified: the README describes a self-hosted server and a playground.

### 2. Where Relay already makes these decisions

Relay has a family of cheap no-tools model calls that are exactly CLM's shape. Each is a JSON-reply prompt to a generative model today. Ranked by fit:

| Seam | Today | As a CLM question | Why it fits |
|---|---|---|---|
| Shell-or-agent routing, `route_assist.py` | Gemini flash-lite over OpenRouter, 0.6–0.9 s | Choice {shell, agent} | Action set fixed forever, only the typed text is embedded per call. Text stops leaving the machine (the router sees everything typed). The deterministic router's `needs_assist` band can widen because assist becomes nearly free. Has tests, a live latency baseline and the owner's labelled examples: **the natural first consumer**. |
| Loop double-check, `loopdetect.run_check` | trigger-only side call | Noul "is this agent stuck?" over the recent calls | Cheap enough to run on every trigger, or every call. |
| Request audit, protocol 12.6, `Agent._start_audit` | Lite tier, off by default | one Noul per request: "was this addressed by the answer?" | At CLM cost it can be default-on. |
| Tier / effort selection for subagents and delegation | none (inherits the parent's model) | Choice {high, main, flash, lite} from the task text | `docs/TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md` P1 item 5 wants a cheaper default for routine work and has no classifier to drive it. |
| Approvals, `approvals.py` | regex classifiers (`DELETE_OR_MOVE`, `NETWORK`), which the module itself calls "classifiers, not proofs" | Noul "does this line delete or move files?" | A second opinion that may **only add an ask, never remove one**. The denylist stays deterministic. |
| Next-command suggestion, `suggestions.py` | generative | `/v1/rank` over recent history and aliases | Ranking known candidates is a better fit than free generation. |

**Verification is the deepest integration.** Relay's QA ladder already has advisory AI rungs (`ai-text`, `ai-visual`), the `ai_may_gate_after N` floor in `qa_policy.py`, and the cases ledger (`cases.py`, #95VZ) that counts verified cases. CLM's `bon_eval.py` scores each trajectory step and takes the mean over the last window (default 12 steps) as the trajectory score. That score is a candidate advisory rung: show it on the card, log it to the ledger beside the human verdict, and let the ledger's calibration data decide when it may gate. It is the one place CLM gives what a text verdict cannot: **a probability, so calibration curves are computable** (what #C3Q2's floor wants and cannot get from prose). It is also where fine-tuning data comes from: checkpoints + transcript + the ledger's `verdict.result` is precisely the trajectory format their evaluator reads (`trajectory_id`, `task_id`, `passed`, per-step embeddings).

**The most ambitious use** is inside the turn loop: score each proposed tool call against the transcript tail before execution. Gives a progress meter in the thinking panel, an early "this is going badly" signal that subsumes loop detection, and eventually best-of-N at the tool-call level with a cheap generator: System One gating System Two. Depends on a Relay-tuned head, so it is last.

### 3. Proposed wiring

One worker module, `relay_core/decisions.py`, with a small interface (`yes_no`, `choice`, `score`, `rank`) and two backends:

- the CLM HTTP client (`POST /v1/systemone`, `/v1/rank`);
- a generative fallback that renders the same question as a JSON prompt through `sidecall.call`.

Register a CLM server the way local models are registered (`localmodels`: probe, saved endpoint, `clm:<id>` preset), and add a `decide` role to the roles table (protocol 13) that falls back Lite → Flash → Main when no server is reachable. Every consumer then works with or without a GPU, and the switch is one Options row. Keep state strings short and stable (rendered by `sidecall.render_transcript` with a cap) so the encoder cache hits. Keep Relay's own naming (yes/no, choice, score), not "Noul".

Security rule: a soft classifier never weakens a hard one. `security.py`'s denylist and the approvals rows stay deterministic; CLM may add an ask or a flag, never grant.

### 4. Order of work, if picked up

1. `decisions.py` with both backends; routing as the first consumer. Measure accuracy on the router test corpus and latency against the current OpenRouter model. About a day, with a clean before/after.
2. Move the loop check and request audit onto it; make the audit default-on.
3. Trajectory scoring as an advisory verification rung, scores written to the cases ledger.
4. Once the ledger has a few hundred verdicts: fine-tune the head on Relay trajectories, evaluate with `bon_eval.py`, let the QA floor decide gating.
5. Tool-call-level scoring and best-of-N, only if step 3 shows signal.

### 5. Open questions

- Is Relay's terminal-agent distribution close enough to Terminal-Bench for the released CLM-8B weights to be useful zero-shot? Step 1 answers this cheaply on the routing corpus.
- Hosted endpoint or self-host only? Unverified. Self-host on the Spark is realistic for the owner; the fallback covers everyone else.
- What is the right state rendering for a terminal agent (transcript tail, last N tool calls, cwd, exit codes)? Their post-training data is agent trajectories, so the shape they expect matters and should be checked against `train/adapters.py` before step 3.
- Where does the score show? Thinking panel, card `verify` block, or only the ledger until it has earned a surface.
