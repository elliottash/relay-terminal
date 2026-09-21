# How quality is assured across fields that use code, and what that means for QA support (research, 2026-09-21)

Card #YZ8G. The owner's definition of QA support began app-shaped — "a user is put into a test case
that simulates the problem the issue was designed to address", an AI trying it first, and that
being what Verify typically is — and he then widened it: "many things people build dont have users,
eg building engineering subsystems, or doing data analysis or econometrics. and within apps, UX can
matter a lot (eg platformer games) or less (eg command-line punctuation fixer on a plain-text file).
and within project, some things are human-facing and some are not", and further: "we should think
about this more broadly as for example scientific / research / knowledge-work automation". His
constraint on whatever follows: "my main worry is that, this approach could get clunky very fast".

Five Opus research passes, each with sources, are kept whole under
[`research/qa-across-fields/`](research/qa-across-fields/): (a) software with users, (b) systems
without users, (c) analysis, statistics and ML, (d) the theory and a per-card classification,
(e) knowledge work and research automation. A sixth pass reviewed the owner's earlier private
project on verification of research work; its ideas are used here in general terms only. This page
is the synthesis. Where a claim carries a number or a quotation, the source is in the named report.

## 1. One idea, found independently in every field

**Manufacture an asymmetry between the maker and the checker, then let the right judge decide.**
The owner's "put a person into a staged case" is the instance where the judge is a person and the
asymmetry is a fresh pair of eyes in a realistic situation. The other instances:

| Field | The staged case | The judge | Where the person sits |
|---|---|---|---|
| Feel-critical games | a level, a fresh player, no hints ("simulate the player in their living room") | the player | in the scenario; first exposure is used up once |
| Most UI work | the computed visual or behavioural diff | a person, in two seconds: intended or regression | beside a diff, not in a scenario |
| CLIs, formatters, compilers | old and new run over a large real corpus (Crater, diff-shades) | the comparison | reads the delta; one question |
| Controllers, embedded | a plant model that reproduces the sensor dropout (hardware-in-the-loop) | an assertion on a trajectory | signs that the evidence is adequate |
| Services, infrastructure | canary, load, injected failure, game day | a threshold with an error budget; three outcomes: pass, fail, **inconclusive → a person** | carries the pager; judges the runbook |
| Refactors, migrations | real traffic mirrored to both implementations | the old implementation | nowhere, unless they disagree |
| Estimators, numerical code | data generated with a **planted truth**; a certified reference; a manufactured solution | tolerance on bias, coverage, order of accuracy | judges whether that was the right truth to plant |
| Data pipelines | rows with an **injected defect** the step must catch; a data diff | the test; for a diff, *no verdict at all* — a named reviewer reads it | reads the diff |
| A paper's tables | a clean-room re-run from raw inputs (the AEA Data Editor's process) | the diff of every number | reads whether the artefact answers its question |
| Literature screening | a gold set of known-relevant studies, withheld | recall | second reviewer over **the rejects only** |
| Reports, memos, briefs | claim-by-claim tracing to sources; an integrity edit | decidable questions per claim | a second independent mind on whether the conclusion is right |
| Lab work | spike-ins, controls, proficiency samples | the known concentration | signs the record |

Three shapes of asymmetry cover all of it (report e): **a known answer that can be withheld**
(planted truth, certified reference, known published result, injected defect, gold set);
**sampling with a materiality threshold and a rejection rule**, for large homogeneous output; and,
where the deliverable is a judgement about what matters, **a second independent mind** — with the
warning that a challenger from the same school produces a foregone conclusion.

## 2. The lines every mature field draws

- **Checking is not testing** (Bach and Bolton): checking is "the mechanistic process of verifying
  propositions" and can be done by a tool; "quality is an opinion, not a fact, so quality cannot be
  verified". That is the `## QA checklist` / `## Human QA` split with no translation. "Checking is a
  tactic of testing": a person's verdict should *generate* the next run's checklist line.
- **Conformance is not correctness** (JPL, 1976): an integrity edit confirms that a cited Figure 1
  exists; "it will not determine whether Figure 1 is actually the figure that is cited in the
  text." Automate the first exhaustively; no rubric rescues the second.
- **Did we build it right / did we build the right thing** (NASA, IEEE 1012). Only the second is
  irreducibly a stakeholder's.
- **Who judges is the four oracle kinds** (Barr et al., 694 papers): specified, derived, implicit,
  none (the human). "It did not crash" is never evidence it is right. The cost of a human oracle
  falls sharply when the scenario is short and made of recognisable data — the research case for
  staging rather than handing over a build.
- **The work that needs a person is named by Valve, not by "has a UI"**: "I know it when I see it;
  has many solutions; subjective; defies direct analysis."
- **A deterministic check can only veto.** Passing means nothing mechanical is broken, not that the
  work is good.
- **Decompose until the question is decidable.** Expert humans agree with each other about 68–71 %
  of the time on open-ended knowledge work (three benchmarks independently); a model asked one narrow
  question — does this page support this sentence — agrees with humans 92–96 %. Collapsing a rubric
  from three levels to yes/no gained about 20 points of agreement. Few, narrow, binary questions;
  one honest open question left to a person; long rubrics buy nothing.
- **Independence is the dial consequence buys**, not volume: aviation's objectives fall 71 → 0 by
  level, but those required "with independence" fall 30 → 0. DOE grades it: peer check, concurrent
  verification, independent verification separated by time and distance. For AI: self-correction
  without external feedback degrades, and judges prefer their own generations — the minimum viable
  AI checker is a different model family that never saw the maker's trace. **The agent that
  implemented a card may not verify it or write its checklist.**
- **Rigour is chosen per item from a risk class** in four industries; the FDA sets it per function.
  NASA: when two classes apply take the higher, and mixed classes in one project are "not uncommon".
  ISO 26262 contributes a level meaning *no special requirements*, and controllability: can it be
  undone.
- **Some evidence should carry no verdict**: a data diff and a match score for a named reviewer.
- **"No findings" must differ from "not reviewed"**: a positive statement that review happened,
  with a date and a name; a time-boxed check says it was time-boxed; a staged test records how its
  environment differs from real conditions.
- **Nobody reviews everything.** One screener misses 13 % of relevant studies, two miss 3 %. The
  best-value design found: one pass forward, the second reviewer over the rejects only — false
  positives surface later, false negatives never do.
- **Give a menu, not a blank box**: a fixed vocabulary of test types made practitioners write twice
  as many tests and find three times as many bugs (CheckList).
- **A model's verdict earns authority by measurement**: every model evaluator carries a validity
  record and gates nothing until it clears a pre-registered threshold for that dimension and that
  action; holistic judgement runs in shadow; agreement among model judges is not validity;
  abstention is scored because it is a gaming surface; using a judge to triage *is* gating, and its
  failure is a missing result no validation sees, so log and sample the rejects; and the metric for
  a critic-and-fixer loop is whether a planted defect *survives* the fix, because an agent learns
  to silence the critic. The characteristic harm of a QA feature is **false reassurance** — making
  you stop looking — so that, not detection rate, is what to measure.
- **No agentic coding product stages a scenario or seeds data for the reviewer.** They hand over a
  diff, a log and a link.

## 3. What a card needs to say, and who says it

Report (d) proposes four questions — who consumes the result (an experience, a decision, a
program, a machine), what oracle exists, what failure costs and whether it can be undone, and
whether a person has already judged this surface — selecting among eight shapes and an
independence rung. That is right as analysis and too heavy as a form. The owner's constraint turns
it into this: **the agent answers the questions at planning time; the card shows one word.**

| Shape on the card | When | What is staged | First player | Judge | The person is asked |
|---|---|---|---|---|---|
| **check** (the default) | a program or machine consumes it and a test can say | the fixture | the runner | the assertion | nothing |
| **diff** | behaviour or appearance changes and the old version, a reference or a corpus exists | old against new | an agent | the comparison; a person only where it differs | one question, seconds |
| **measure** | the quality is a number: latency, memory, build time, accuracy against planted truth | a load, a simulation, a canary | the pipeline | a threshold; inconclusive goes to a person | nothing, unless inconclusive |
| **scenario** | an experience, or "I know it when I see it" | the smallest realistic situation in which the card's problem is happening | an agent, which clears every checkable step | the person | one question per scenario, three at most |
| **read** | the deliverable is a judgement: a report, a table for a paper, a design | the reproduction and the claim-by-claim trace | an agent | a second independent mind | is the conclusion right |

A card that claims "better than" adds a baseline to whichever shape it has. A person's verdict on
a scenario turns that scenario into a check the next time the same surface changes.

## 4. Phasing, under the owner's constraint

Rules first: the default costs zero clicks; the tool proposes and the person overrides, never a
form; one question per scenario with a stated time budget; never the same question twice; no new
card section unless the shape needs it; and the feature measures its own weight — a skipped brief
says the brief was too heavy.

1. **Now, nothing to build.** `## Tests` with its veto gate, a mechanical `## QA checklist`,
   `## Human QA` by hand when wanted. #7BM4 is the worked example.
2. **One derived word.** The planning agent proposes the shape with a one-line reason; the card
   shows a chip; one click changes it. Two rules come free: the verifier is not the implementer
   (Relay already recommends a different model family), and a verification writes a positive
   statement that it happened.
3. **The staging button** for `scenario` cards: after Verify, the agent stages the case, plays
   it, and hands over the brief. Records how the staged environment differs from real use.
4. **Evidence without a verdict** for `diff` cards: old against new, a two-second yes or no.
5. **Earned authority.** Plant a defect in fifteen merged commits, keep fifteen clean, measure
   what the AI verifier catches, what it flags falsely, and whether it cites the right line. Until
   then it advises and never moves a card. Afterwards: sample cards that passed, re-audit cold,
   report the false-reassurance rate.
6. **Later, if wanted.** Risk classes that change who may judge; `measure` with canaries; `read`
   for research and knowledge work; a second-reader state for the few irreversible cards.

Build through 3, use it for some weeks, and let the places where a person was asked something
they did not need to be asked decide the rest.
