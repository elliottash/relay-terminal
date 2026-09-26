---
id: YZ8G
type: work
status: planned
labels: [feature, switchboard, qa]
assignee: claude-code
priority: 1
rank: zzzzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [HKAP, PR4Q, WC3E, JNYN, 74Y5, BX7B, P7CF, SJTR, 1QKM, GW74, C3Q2, 1AA6], github: null}
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
- 2026-09-26 — Owner: “yes to all. 7z8g 3 put this on another card for later”. Close this scoping umbrella after #PR4Q, #WC3E and #JNYN. Track observed-use evidence separately; put Compare results and Results list on a later card; add planted defects and clean controls to #P7CF’s pilot.

## Plan
**Freshened 2026-09-26 (stale-card review, HEAD `6514be0c`).** The research and scoping the Issue asked for are done: `docs/QA-ACROSS-FIELDS-RESEARCH.md` and `docs/research/qa-across-fields/`. The slice adopted on 2026-09-21 (steps 1–3) has landed as child cards. Newer cards now carry the wider direction. Three things are left on this card: closing the three child cards, the owner's "build 1–3, then use it" pause, and deciding where steps 4–7 go. It builds nothing itself. The 2026-09-21 plan text, including its three owner questions (answered "i agree with all, go ahead with it"), is in git history (this file at `6514be0c`) and in the thread.

**The design in five lines** (adopted 2026-09-21, unchanged)
1. The card states the intended outcome and how failure would be recognised — before the work.
2. The agent prepares the changed artefact and the applicable evidence.
3. Relay shows what passed, what failed, what evidence is missing, and what remains unanswered.
4. When judgement is needed, Relay opens the relevant thing and asks one specific question.
5. Acceptance records the evidence and the decision for that revision.

**Where each step stands**
1. **Gate** is #PR4Q. It landed (`60b3fa43`, `2d1901e6`, `630db31e`, `c3a88caa`), was verified with no findings (`fcb7fad7`), and sits in `needs-qa-llm`.
2. **Expectations before the work, and a verification record** is #WC3E. It landed (`0ef3ee13`, `486852e0`), was verified with no findings (`eb49e562`), and sits in `needs-qa-llm`. The rule is now in force: `.board/POLICY.md` rules 5 and 11 and `## Done means` before code. `verified()` (`backend/relay_core/board.py:759`, #1AA6 `c40c6695`) gates `done` on the `verify` block.
3. **Try it** is #JNYN. It landed (`03701acf`, `bca9a82e`, `1f3a7af0`, `132d3523`, …) and sits in `needs-verification`, idle since 2026-09-22. Its `## Verdict` passes the two repaired defects, but "broader staging verdict PENDING a2": the combined current-build staging run died in the 2026-09-22 API overload and was never re-run. #74Y5 ("built to be driven", `ece752ef`, `e0442a22`) is `done`.
4. **Compare results** is not built and has no card. The nearest piece is #BX7B's Review pane, which shows a card's result and evidence (landed, `needs-verification`).
5. **Observed use** is not built and has no card. #P7CF (backlog closure, `planned`) takes a different route: a manifest plus a stratified pilot sample.
6. **Results list** is not built and has no card. #1QKM (`discussing`) is the product home for knowledge-work deliverables.
7. **AI-verifier evaluation on planted defects** is not built as specified. #P7CF's pilot measures real defect and missing-evidence rates. #C3Q2's `ai_may_gate_after` (`backend/relay_core/qa_policy.py`) and #GW74 (earned authority, `discussing`) decide how much authority an AI verifier gets.

**Cards that grew out of this one:** #BX7B (Review pane and the human-QA designation: the owner's "a card has a designation of whether human QA is needed", now `verify.human`), #1AA6 and #C3Q2 (`verified()` and the QA policy floor), #P7CF, #SJTR (a QA attack system, `discussing`), #SW1D (Hygiene / Performance, `needs-qa-llm`) and #GW74.

**Codex's policy contradictions** are resolved in `.board/POLICY.md`. Rule 10 lists `Human QA`, `Profile` and `Try it`. The implementer writes `## Done means` and no `## QA checklist`; the verifying session writes the record. Agents move cards within their authority, and a card with an open judgement waits for the person.

**Close when (proposed):** #PR4Q, #WC3E and #JNYN have left the QA lanes with a verdict, and the owner has filed or dropped each of steps 4–7 (questions on the thread, 2026-09-26).

**Risks:** #JNYN's `assignee: codex-verify-jnyn-a1` is a dead verifier session; a new verifier must take it over explicitly. Under `verification: ask`, #PR4Q and #WC3E wait for the owner to close them, and #P7CF's `authorized_by` close is not built yet.
**2026-09-26 scope update.** The owner approved closing this scoping umbrella after #PR4Q, #WC3E and #JNYN finish. Observed-use evidence is #SWQN; Compare results and Results list are deferred to #DNYG; planted defects and clean controls belong in #P7CF's pilot.

## Tasks
- [x] Research and scoping; plan adopted 2026-09-21 — `docs/QA-ACROSS-FIELDS-RESEARCH.md`, `9b219db2` <!-- t:y1 -->
- [x] Step 1, the gate (#PR4Q): landed and verified — `60b3fa43`…`c3a88caa`, `fcb7fad7` <!-- t:y2 -->
- [x] Step 2, expectations and the verification record (#WC3E): landed and verified — `0ef3ee13`, `486852e0`, `eb49e562` <!-- t:y3 -->
- [x] Step 3, Try it (#JNYN): landed — `03701acf`, `bca9a82e`, `1f3a7af0`, `132d3523` <!-- t:y4 -->
- [x] Built to be driven (#74Y5): done — `ece752ef`, `e0442a22` <!-- t:y5 -->
- [x] Human-QA designation gates "verified" — #1AA6 `c40c6695`, Review pane #BX7B <!-- t:y6 -->
- [x] Policy contradictions resolved in `.board/POLICY.md` <!-- t:y7 -->
- [ ] #JNYN: a fresh independent verifier completes the combined current-build staging run (its Done means 1, 2 and 5 are pending) <!-- t:y8 -->
- [ ] #PR4Q and #WC3E: owner closes them from `needs-qa-llm` <!-- t:y9 -->
- [x] Observed use and owner trial of Try it / Review tracked on #SWQN <!-- t:yb card=SWQN -->
- [x] Owner answered scope questions; Compare results and Results list transferred to #DNYG, planted-defect pilot to #P7CF <!-- t:yc -->
- [ ] Close this card <!-- t:yd -->
