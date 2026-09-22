---
id: SJTR
type: work
status: discussing
labels: [feature, switchboard, qa]
component: [gui, worker]
waiting_on: owner
parent: YZ8G
rank: zzzzzzzzzzzzzzzzg
created: '2026-09-21'
source: owner, 2026-09-21
links: {plans: [], commits: [cf2089d68f62f618e29830d78f28c92e884c2965], evidence: [reports/Relay QA attack system.md, research_notes/Relay QA attack system/], related: [YZ8G, SW1D], github: null}
---
# Tests becomes the front door to a QA attack system: agents that try to break the project

## Issue
"lets keep Tests for now. but i am thinking that should link to a broader QA system page, where
agents will attack your project to find errors"

## Decisions
- 2026-09-21, owner: the Tests button/pane keeps its name (superseding the "Validation" rename
  discussed on #SW1D), and becomes the entry point to a page where agents actively try to break
  the project, not only report the tests it already has.

## What this already connects to

The five research passes and Codex's review done for #YZ8G already cover most of the ground this
idea needs (`docs/research/qa-across-fields/`, `docs/QA-ACROSS-FIELDS-RESEARCH.md`):

- **Fuzzing** (OSS-Fuzz) and **differential testing** (N-version, pseudo-oracles, Csmith-style
  generators) — report (b), §Barr et al. oracle categories.
- **Chaos engineering / game days** (Netflix, Google DiRT) — report (b): "stage the adverse
  situation the work was written to survive, let a non-human judge decide" is exactly "an agent
  attacks the project"; the judge there is an assertion or a threshold, not a person.
- **Mutation testing** — report (d) flags it as real but "far too slow for a button" for a
  per-card Check; that argues against putting it on the card-level gate and *for* a scheduled or
  on-demand background run reported here instead.
- **Red-teaming and adversarial review** — report (e): structured analytic techniques, and the
  SSCI's 1978 warning that a challenge function staffed from one school of thought produces a
  foregone conclusion — an attacking agent should not be the same model family as the code it is
  attacking, same rule already adopted for Verify.
- **Provenance's W7-CRITIC protocol** (the owner's own earlier project, prior art, kept general in
  the public docs per the earlier reviewer's note) is the closest existing design for exactly this:
  an agent that finds defects only counts once its detection rate, false-positive rate and
  citation correctness are measured against planted and externally-documented defects, run in
  shadow before it gates anything. The same "earned authority" rule already in #YZ8G's plan
  applies here, more so — an attacking agent that cries wolf is the fastest way to make this
  clunky, which is the standing worry on this whole line of work.
- **Already available in this environment**, not hypothetical: the `security-review` skill and
  `/code-review` (including `ultra`, multi-agent cloud review) already do a version of this over a
  diff. The attack page may be substantially built by scheduling and surfacing what these already
  do, rather than a new engine.

## A design sketch, not yet a plan

- The Tests pane keeps its current job (are the known tests green, reliability, history) and
  gains a second section: findings from agents that tried to break something — a fuzz run, a
  differential run against a reference, a red-team pass over recently changed code, a periodic
  mutation-testing score — each finding routed the way a flaky test already is: **Make a card**.
- Every attacking agent's findings carry a validity record before they gate anything (advisory
  only until measured), per the earned-authority phase already planned for #YZ8G's own AI
  verifier — the same discipline, applied to a harsher judge.
- This is bigger and riskier than anything else on #YZ8G's plan, and needs its own scoping pass
  before a `## Done means` can be written honestly.

## Questions for the owner

1. Scope the research now, or hold this as a backlog idea until the smaller pieces (#SW1D, #1CXD,
   #74Y5) land? **Recommendation: hold.** Most of the grounding already exists in the five reports;
   a short follow-on pass on fuzzing/mutation-testing tooling actually available for this repo's
   stacks (C++/Qt, stdlib Python, no pytest) is cheap when it's time, and nothing here is urgent.
2. Should the first attacking agent be a code-review pass (cheapest, already exists as a skill) or
   something new like fuzzing? **Recommendation: code review first** — it needs no new
   infrastructure and gives an early read on the false-positive rate this whole idea lives or dies
   by.

## Planning notes
Research requested by owner, 2026-09-21; full synthesis: `reports/Relay QA attack system.md`, supporting notes: `research_notes/Relay QA attack system/`.

Discussion recommendations, not approved implementation scope:
- Keep Tests as entry point; distinguish known checks from advisory findings. Separate discovery, replay/adjudication, and repair.
- Start with bounded, on-demand review of one frozen change and one executable attack harness; evaluate usefulness before recurring scheduling or gates.
- Reuse Tests history/UI, jobs, generic agents and confirmed-failure signals. Attack/finding records, complete replay manifests, durable scheduling and filesystem/network containment are additional work.
- Mutation tests test-suite sensitivity; Mull supports changed-line runs, so scheduling should follow measured cost rather than a categorical rule.
- W7-CRITIC originals were found, but completed empirical results were not located. Adapt its matched controls and externally documented defects; do not describe it as validated deployment.
- Different-family review is a recommendation/hypothesis, not proof of independence. Measure calibration and human triage cost.
- Installed security-review / ultra cloud-review capability is unverified; generic agents are verified in local code.

The earlier recommendation to hold research is superseded by the owner's request to research now. Feature remains discussing, waiting on owner; no attacks or implementation authorized by this research.
