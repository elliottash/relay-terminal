---
id: YZ8G
type: work
status: executing
labels: [feature, switchboard, qa]
assignee: claude-code
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
- **2026-09-21 — the four questions, answered.** (1) "OK": the shape words are check, diff, measure, scenario, read. (2) "i think cards need to be moveable by agents": they stay movable; the implementer moves its card to needs-verification, the verifier moves it to QA or back, and only a judgement-shaped card waits for the person before Done. (3) "yes": build through phase 3, then use it. (4) Not separate models — a separate session: the agent that built a card does not write its checklist; the verifier writes it from what it checked.
- **2026-09-21 — quiet time in QA counts, if measured.** "if a card has been in QA for a while and hasnt resurfaced, is that evidence its fine? any way to systematize that? eg usage logs indicating usage of features that the card touches." Yes, as *proven in use*: a card's commits name its files, Relay's local logs name the actions, panes and tools exercised, and faults are already collected (crashes, worker faults, #AQ6X signals). A card in QA accumulates exposure and, past a threshold, proposes its own closing with the numbers; a surface never used is not evidence and the card stays. No telemetry: all local.
- **2026-09-21 — keep it plain.** "you lost me a little bit with the latest discussion of laboratories, corpora, etc." The research stays in the docs; the card says only what gets built.
- **2026-09-21 — the knowledge-work question was about the product, not the method.** "i was not looking at knowledge work as a metaphor for verification. i was asking, how will knowledge workers use relay terminal". The research passes answered the method question; the product question is open: what a researcher's, analyst's or scientist's cards are (claims and artefacts: a table, a figure, a dataset, a screen, a section), what "done" means for each (reproduces from raw; numbers match the text; claims trace to sources; not stale), and what the tooling row becomes (Results pane, Check as reproduction and reconciliation, Profile as reproduce-from-raw, proven-in-use as cited-and-unchanged, Human QA as a coauthor's read). Prior art: the owner's Provenance ("the agent is a contractor inside an instrumented environment; the researcher is the auditor"). Next: a product pass on three people, the economist first.
- **2026-09-21 — a skeptical second opinion, and product simulations, from Codex.** "can you query codex cli to give you a skeptical take and to see if this is forced, to see how we keep it light enough to accommodate these different use cases. then have codex give you the product simulations as well". Codex's verdict: the shape-per-card taxonomy is forced; keep actions, drop card types; the gate needs four statuses and must accept attached results; "proven in use" is "observed use" and never closes a card; acceptance expectations come before implementation; build a smaller slice than phase 3. Its simulations (economist, policy analyst, computational scientist) say: build once a route from request to deliverable, an output record and an acceptance record; **Results is separate from the Test suites pane**; field conventions and viewers stay out of the core. Both documents under `docs/research/qa-across-fields/` (f, g). The plan below is rewritten to that slice and waits on the owner.
- **2026-09-21 — the revised plan is adopted.** "i agree with all, go ahead with it": the five-line design, steps 1–3 first, Results separate. Steps filed as #PR4Q (the gate), #WC3E (expectations and the verification record), #JNYN (Try it), each with `## Done means` before the work.
- **2026-09-21 — for apps, three steps.** "the agents should: 1) run tests 2) verify with an AI simulator 3) open the app for the human in a simulated environment that tests that issue." Mapping: (1) is Tests/Check/Run these; (2) moves into Verify (#WC3E): the verifier stages and plays the app and records `tests:` and `simulation:`; (3) is the Try it button (#JNYN), which opens what Verify staged and stages itself only if Verify never ran.
- **2026-09-21 — the QA agent walks the user experience and spends human time last.** "i think the QA agent should also always think about each step of the user experience , and apps should be built for live simulated drives by agents and humans -- the QA agent tries to optimize this to conserve scarce human tester time". The agent does design the simulation (the Verify brief stages from Issue and Done means). Filed as #74Y5: the objective in both briefs, a numbered user path with each step marked check / agent / person, and Relay driveable by name rather than by pixel.
- **2026-09-20 — first worked example: #7BM4.** `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/`: `scenario/stage.py` stages a project in which the card's three problems are happening, `scenario/scenario.json` is the scenario as data (situation, what one did before, steps with an actor of `both` or `human`, an expectation each, one question for the person), `scenario/ai-pass.sh` + `ai-pass.md` are the AI's pass with a screenshot per step, and `HUMAN-QA.md` is the brief generated from the same data.
- **2026-09-20 — first concrete piece: a demo button after Verify.** "a first example of this woudl be a button after "verify", available once you are in needs verification or later. the agent would then run your app in a way that illusrates the feature or fix"
- **2026-09-20 — the QA pane is linked back to the card, like Execute and Verify.** "something else -- we can do the same linking of the QA-support agent pane and the switchbaod card we did with execute and verify: #HKAP" — the demo hand-off writes the pane's `pane_token` into its thread entry, so the entry is a link that reveals that pane (`WindowManager::focusPane`, inert once closed). The mechanism already ships (#HKAP, 56b921f6 + 31468891); this card reuses it rather than adding a second one.

## Plan
**Revised 2026-09-21 after Codex's review; the previous plan (five shape words, six phases) is in
the thread and in `docs/QA-ACROSS-FIELDS-RESEARCH.md` §3–4 with §5 saying what changed.**

**Goal** — From a request to the current deliverable, its supporting evidence and the one decision
that is still a person's, in as few steps as the work allows. No card is classified; an agent
proposes the next useful action. Nothing closes on quiet logs. Research: `docs/QA-ACROSS-FIELDS-RESEARCH.md`.

**The design in five lines** (Codex's, adopted)
1. The card states the intended outcome and how failure would be recognised — before the work.
2. The agent prepares the changed artefact and the applicable evidence.
3. Relay shows what passed, what failed, what evidence is missing, and what remains unanswered.
4. When judgement is needed, Relay opens the relevant thing and asks one specific question.
5. Acceptance records the evidence and the decision for that revision.

**Actions on a card, proposed by an agent, never mandatory**: Run checks · Compare results · Try
it · Review. For an app the thing opened is a staged interaction; for a backend a request and
response or a failure reproduction; for an analyst changed rows and reconciled totals; for a
researcher a figure, its reproduction and the claim it supports.

**Steps**
1. **Fix the gate and the duplicated records** (#7BM4's own follow-up). Statuses: passed, failed,
   missing evidence, not applicable. Accept an attached result (CI, a collaborator's run) tied to a
   revision and environment. A retired test is "replace retired check", not "gone". An override is
   scoped to a check and a revision and expires. Required checks come from the agreed outcome, so a
   card with no `## Tests` is not thereby ungated. The latest status replaces the pile of dated
   `### Check` blocks; history behind a link. A run updates the status without another Check.
2. **Expectations before implementation, and a verification record.** The planning turn writes
   what "done" means and how failure would show, in the card. A separate verification session
   (not the implementer) records the revision it checked, the evidence, and what is unresolved,
   with a positive statement that it happened. Agents move cards within that; a card with an open
   judgement waits for the person. No chip. No closing from usage.
3. **Try it**, on any card: one action that stages the situation (a pinned build, a disposable
   fixture), completes the mechanical pass, and hands the person one short task and one question
   — without the answer in the brief. If staging fails, that is reported instead of a request.
4. **Compare results**: changed output beside its baseline — screenshots, rows, tables, files —
   with uncertain and unexpected results allowed; a person's yes or no where they differ.
5. **Observed use**: a card in QA shows "used N times over D days; no captured faults" from local
   logs, as information only.
6. **Results**: a compact list beside the board, when outputs exist, of selected deliverables with
   their producing command, code and inputs, checks and review; opens each in its own application.
   Separate from the Test suites pane; not a fifth toolbar button.
7. **Evaluate the AI verifier** on a pilot of planted defects and clean controls before it is
   trusted beyond advice; report missed defects, false alarms, unjustified passes and the
   reviewer's effort.

Build 1–3, then use it. 4–7 by need.

**Contradictions to resolve in the policy text** (Codex): agents may move cards versus the
verifier may not (resolved above: within authority, judgement waits); the policy's "supply a
checklist" versus "the implementer does not write it" (the verifier writes the record; the
implementer writes expectations); the policy's fixed headings omit `Human QA` and `Profile`.

**Questions for the owner**
1. Adopt this revised plan in place of the five-shape, six-phase one? Recommendation: yes.
2. Steps 1–3 first, then use it? Recommendation: yes; #7BM4's gate fix is step 1 and is small.
3. Results as a separate list beside the board, not a rename of the Test suites pane?
   Recommendation: yes, as Codex argues: a figure needs no test and a test makes no deliverable.
