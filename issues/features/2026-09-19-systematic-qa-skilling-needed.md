---
id: YZ8G
type: work
status: discussing
labels: [feature, switchboard, qa]
waiting_on: owner
priority: 1
rank: zzzzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [HKAP], github: null}
---
# systematic QA skilling needed

## Issue
relay should help users with human QA. deep research and scoping needed on this.

## Decisions
- **2026-09-20 — what "QA support" means.** "in my mind, QA support is 'human QA', as in a user is put into a test case that simualtes the problem the issue was designed to address"
- **2026-09-20 — an AI plays the scenario first.** "thats good to design for an AI to try simulating first, if we dont have that as well." We did not have it: the LLM QA lane and Verify run a card's checklist; nothing staged a scenario and played it in the app.
- **2026-09-20 — and that is what Verify typically is.** "i think verification should involve that (typically)"
- **2026-09-21 — two sections on a card, not one.** Asked whether human QA is part of `## QA checklist` or its own section, the owner: "i tend to agree" with two. `## QA checklist` stays mechanical — every line names the command or screenshot that proves it, an AI verifier re-runs and ticks it, and the AI's scenario pass is one of its lines. `## Human QA` holds the scenario link, one question per scenario and the person's answers in their own words. A card with nothing a person must judge has no such section. (Its final name waits on the wider research below.)
- **2026-09-21 — the app-focused view is too narrow.** "i have been taking an app-focused view here. but many things people build dont have users, eg building engineering subsystems, or doing data analysis or econometrics. and within apps, UX can matter a lot (eg platformer games) or less (eg command-line punctuation fixer on a plain-text file). and within project, some things are human-facing and some are not. … so we can take a broader approach to his issue." Research commissioned: how QA is done across fields with code and computing.
- **2026-09-21 — wider than econometrics: research and knowledge work.** "re 3 econometrics -- thats of course my personal interest, but its niche and we should think about this more broadly as for example scientific / research  / knowledge-work automation"
- **2026-09-21 — phase it; it must not get clunky.** "great, and i want to phase this -- my main worry is that, this approach could get clunky very fast". Taken as the design constraint on everything in `## Plan`.
- **2026-09-20 — first worked example: #7BM4.** `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/`: `scenario/stage.py` stages a project in which the card's three problems are happening, `scenario/scenario.json` is the scenario as data (situation, what one did before, steps with an actor of `both` or `human`, an expectation each, one question for the person), `scenario/ai-pass.sh` + `ai-pass.md` are the AI's pass with a screenshot per step, and `HUMAN-QA.md` is the brief generated from the same data.
- **2026-09-20 — first concrete piece: a demo button after Verify.** "a first example of this woudl be a button after "verify", available once you are in needs verification or later. the agent would then run your app in a way that illusrates the feature or fix"
- **2026-09-20 — the QA pane is linked back to the card, like Execute and Verify.** "something else -- we can do the same linking of the QA-support agent pane and the switchbaod card we did with execute and verify: #HKAP" — the demo hand-off writes the pane's `pane_token` into its thread entry, so the entry is a link that reveals that pane (`WindowManager::focusPane`, inert once closed). The mechanism already ships (#HKAP, 56b921f6 + 31468891); this card reuses it rather than adding a second one.

## Plan
**Goal** — QA support picks, per card, the lightest QA that can honestly say the card did what it
was for: most cards get nothing new, a person is asked only what a person must judge, and an AI's
verdict counts only as far as it has been measured. Research and sources:
`docs/QA-ACROSS-FIELDS-RESEARCH.md` (synthesis) and `docs/research/qa-across-fields/` (five reports).

**What the research settled**

1. Every field does the same thing — manufacture an asymmetry between maker and checker, then let
   the right judge decide. "Put a person into a staged case" is the instance where the judge is a
   person. Elsewhere the staged case is a plant model, a canary, a corpus run, data with a planted
   truth, an injected defect, a withheld gold set, or a clean-room re-run; and where the deliverable
   is a judgement, the only honest QA is a second independent mind.
2. `## QA checklist` is *checking* (propositions, a tool can do it) and `## Human QA` is *testing*
   ("quality is an opinion, not a fact, so quality cannot be verified"). A deterministic check can
   only veto. A person's verdict should generate the next run's checklist line.
3. The work that needs a person is not "has a UI" but Valve's "I know it when I see it; has many
   solutions; subjective; defies direct analysis". Most UI work needs a diff and a two-second yes or
   no, not a scenario; a CLI needs a corpus run and one question; a subsystem needs no person at all.
4. Decompose until the question is decidable: experts agree with each other ~70 % on open-ended
   work, a model agrees with them 92–96 % on one narrow question. Few, binary questions; one open
   one for the person; long rubrics buy nothing.
5. Independence is what consequence buys. The agent that implemented a card may not verify it or
   write its checklist; the minimum AI checker is a different model family that never saw the
   maker's trace.
6. A model's verdict earns authority by measurement against planted defects, holistic judgement
   runs in shadow, and the harm to measure is false reassurance. No agentic coding product stages a
   scenario for its reviewer.

**The shape a card carries** — one word, proposed by the planning agent with a one-line reason,
changed by a click: **check** (default; the runner judges; the person is asked nothing), **diff**
(old against new; a person only where they differ), **measure** (a threshold on a number;
inconclusive goes to a person), **scenario** (the smallest realistic situation in which the card's
problem is happening; an agent plays it first and clears every checkable step; one question per
scenario, three at most), **read** (the deliverable is a judgement; an agent reproduces and traces
it; a second independent mind says whether the conclusion is right).

**Rules that keep it light** — the default costs zero clicks; the tool proposes and the person
overrides, never a form; a stated time budget on every brief; never the same question twice; no
new card section unless the shape needs it; "no findings" differs from "not reviewed" by one
written line; a skipped brief counts against the feature, not the person.

**Phases** — each useful alone; stop after any.

1. Nothing to build: `## Tests` with its veto gate, a mechanical `## QA checklist`, `## Human QA`
   by hand. #7BM4 is the worked example.
2. The derived shape word and its chip; the verifier is never the implementer; a verification
   writes a positive statement that it happened.
3. The staging button for `scenario` cards, after Verify: stage, play, hand over the brief, and
   record how the staged environment differs from real use. (Decision 1's demo button.)
4. Evidence without a verdict for `diff` cards.
5. Earned authority: fifteen merged commits with one planted defect each and fifteen clean
   controls; detection, false positives, and whether the finding cites the right line. Until then
   the AI verifier advises and never moves a card. Then sample passed cards and report the
   false-reassurance rate.
6. Later, if wanted: risk classes that change who may judge, `measure` with canaries, `read` for
   research and knowledge work, a second-reader state for the few irreversible cards.

Build through 3, use it for some weeks, and let the places where a person was asked something they
did not need to be asked decide the rest.

