# How QA is actually done in software people use — across the whole "does the experience matter?" spectrum

Research slice: software with users, from a platformer's jump to a punctuation-fixing CLI.
Date: 2026-09-21. Read-only research; nothing in the repo was touched.

**Method note.** Every claim below is tagged with a URL. Where I could only get a secondary
source, or only a search-engine summary rather than the page itself, I say so inline and again
in "What I could not verify" at the end. The WebSearch budget ran out partway through, so a few
items I wanted (Firefox's `qe-verify` flag, Chromium's `Test-Manual` label, a real enterprise UAT
script template, a primary source on bug bashes) are unverified and flagged.

**The one sentence that most repays reading.** Valve's own GDC 2006 deck on Half-Life 2 opens by
naming the class of problem that forces human QA at all — "The Fuzzy Problem of [design]: Obvious
in hindsight — *I know it when I see it*; Has many solutions; Subjective; **Defies direct
analysis**."
(extracted verbatim from the slide text of
<https://www.gamedevs.org/uploads/valve-design-process-for-creating-half-life2.ppt>)

"Defies direct analysis" is the right gate for the feature being designed. Not "does it have
users", not "is it UI". The question per card is: *is there a property of this change that a
machine cannot compute an answer for?*

---

## 1. Games, especially feel-critical ones

### 1.1 Playtesting: Valve's rules, verbatim from the deck

Valve's "Design Process for Creating Half-Life 2" (GDC 2006, Robin Walker / David Speyrer) is the
most-copied primary source in the field. Its playtesting slides say (text extracted directly from
the .ppt; apostrophes lost in extraction, restored here):

- **Who must be present:** "Make sure the people responsible for the design and execution are
  there." Effects listed: "Simplifies evaluation / Prioritizes / Motivates."
- **How the session is run:** "Simulate the player **in their living room**" — "Don't give them
  hints", "Don't answer any questions", "Don't provide extrinsic rewards".
- **Who tests:** "Use external playtesters."
- **What you get from it:** "Don't rely too much on questions — oftentimes you learn more from what
  playtesters *don't* experience." "Ask non-leading questions."
- **Timing:** "Late playtesting is less valuable — it's too late to make substantive changes."
- **Objection-handling:** "Don't let theoretical problems prevent playtesting. They might not
  actually be problems. If they are problems, the playtest will prioritize which to solve first."
- **What playtesting is for, politically:** "A great way to avoid design arguments" and "Easy to
  measure an element's incremental value or damage."
- **What cannot be measured until the whole thing exists:** "Some aspects of your game can't be
  measured until it's all there: **Pacing / Difficulty curve / Variety / Chapter-to-chapter
  inconsistencies**."

Chet Faliszek's public framing of the same practice: playtesting is "just proof to you. There's no
way you can argue with that" — i.e. its product is *evidence that ends an argument*, not a bug list
(<https://www.gamedeveloper.com/game-platforms/valve-s-faliszek-playtesters-aren-t-idiots-it-s-you>).
A practitioner write-up of the deck adds the worked example of designers noticing players covering
their own mouths to stay quiet near a pursuing monster, and then building that into the game —
an observation no questionnaire would have produced
(<https://www.jeizzon.com/writings/playtesting-like-valve>).

### 1.2 Kleenex testers

Defined by a working AAA developer: "Kleenex testers are only ever brought in once to try the game
fresh. They are not told what they will be testing beforehand, have signed the NDA, and will never
be asked back again." They are usually pointed at one thing — UI, tutorial, a single feature — not
the whole game, and are run "every week or two" alongside "multiple internal playtests every week"
(<https://www.tumblr.com/askagamedev/701096843155734528/how-often-are-playtesters-used-in-game>;
practitioner Q&A, not a studio document).

**The idea worth stealing:** first-exposure is a *consumable resource*. A person who has already
seen the card can never again tell you whether the thing is discoverable. This is the single
strongest argument for the owner's "AI plays the scenario first, then a human" ordering — but it
also warns that the human's freshness is spent on the *first* human run, so the staged scenario has
to be worth it.

### 1.3 Dedicated QA as a separate discipline

Nintendo's is a whole company: Mario Club Co., Ltd., spun out as a subsidiary in July 2009, 432
employees as of September 2025, doing "testing, quality control and debugging for Nintendo
published titles" (<https://en.wikipedia.org/wiki/Mario_Club>). The structural point: feel-critical
studios separate *debug/coverage QA* (a large, scripted, repeatable workforce) from *playtesting*
(small, fresh, unscripted, observed by the designer). They are different artefacts answering
different questions, and conflating them is a known failure.

### 1.4 What is mechanised in game QA

- **Frame-timing / jank regression.** The clearest published thresholds are Android's, and they
  transfer: the frame deadline is 16 ms at 60 fps, 11 ms at 90, 8 ms at 120; exceeding it by 1 ms
  makes `Choreographer` drop the whole frame. "Slow frames" 16–700 ms, "frozen frames" 700 ms–5 s.
  Regression is caught by Macrobenchmark's `FrameTimingMetric` in CI and by JankStats +
  Play's Android vitals in production
  (<https://developer.android.com/topic/performance/vitals/render>). A number like this is
  fully machine-checkable and should never go to a human.
- **Automated playthrough bots.** EA's "Augmenting Automated Game Testing with Deep Reinforcement
  Learning" (arXiv 2103.15819) uses self-learning agents to increase test coverage, find exploits
  and unintended mechanics, and probe map difficulty — explicitly framed as augmenting, not
  replacing, "human play testers, play test scripting, and prior knowledge"
  (<https://arxiv.org/abs/2103.15819>).
- **Telemetry heatmaps** are used to *locate* where to look (death clusters, drop-off points), not
  to decide whether the feel is right. I could not fetch a primary source for the canonical
  heatmap practice (see unverified list).

### 1.5 "Game feel" — what studios say only a person can judge

Swink's definition: game feel is "real-time control of virtual objects in a simulated space, with
interactions emphasised by polish", and he explicitly tries to put metrics behind words like
"tight" and "floaty" — input response curves, the shape of a jump arc
(<https://en.wikipedia.org/wiki/Game_feel>; the primary PDF
<https://dtc-wsuv.org/wp/dtc338-engines/files/2017/01/Mechanics-and-Metrics-of-Game-Feel-Steve-Swink.pdf>
exceeded the fetch size limit, so I am relying on the encyclopaedia summary for the definition).

The practical split that falls out of Swink + Valve:

| Property of a jump change | Machine? |
|---|---|
| input-to-first-pixel latency in ms | yes |
| apex height, airtime, coyote-time window in frames | yes |
| does the player reach the ledge / is the level completable | yes (bot) |
| does it *feel* heavy, floaty, snappy, mushy | **no** |
| do players now try the jump they were previously afraid of | **no** |
| is the difficulty curve right across the chapter | **no** (Valve: "can't be measured until it's all there") |

---

## 2. Consumer and productivity apps

### 2.1 Usability testing — and how a task scenario is written

This is the format the owner's "staged test case" should copy, and NN/g's rules are specific.
A task scenario is "the action that you ask the participant to take on the tested interface",
wrapped in "a short scenario that sets the stage for the action". The rule: "Provide the
participant with all the information that she needs to complete a task, **without telling her
where to click**." Three tests, each with NN/g's own before/after
(<https://www.nngroup.com/articles/task-scenarios-usability-testing/>):

- **Realistic** — bad: "Purchase a pair of orange Nike running shoes"; good: "Buy a pair of shoes
  for less than $40".
- **Actionable** — bad: "Tell me where you'd click next"; good: "Use www.fandago.com to find a
  movie you'd be interested in seeing on Sunday afternoon".
- **Doesn't give away the steps** — bad: "Go to the website, sign in, and tell me where you would
  click"; good: "Look up the results of your midterm exams".

Two related NN/g resources I saw listed but did not read: a 7-step method for turning research
goals into scenarios (<https://www.nngroup.com/articles/ux-research-goals-to-scenarios/>) and
MeasuringU's "Seven Tips for Writing Usability Task Scenarios"
(<https://measuringu.com/task-tips/>).

**Sample size.** Nielsen & Landauer's model: problems found = N(1 − L)^n, with L ≈ 31% per user;
five users surface ~85% of problems, and "the ultimate user experience is improved much more by 3
studies with 5 users each than by a single monster study with 15 users"
(<https://www.nngroup.com/articles/why-you-only-need-to-test-with-5-users/>). The operational
reading for a Kanban card: **one fresh person per iteration of the card beats five people once.**

**Think-aloud** is the standard protocol (participant narrates while working; the facilitator does
not help — the same rule as Valve's "don't answer any questions").

### 2.2 Heuristic evaluation

Expert-inspection alternative when you have no users to recruit. Nielsen's 10 heuristics, named:
visibility of system status; match between the system and the real world; user control and freedom;
consistency and standards; error prevention; recognition rather than recall; flexibility and
efficiency of use; aesthetic and minimalist design; help users recognize, diagnose and recover
from errors; help and documentation
(<https://www.nngroup.com/articles/ten-usability-heuristics/>). These are checkable *by an AI* as a
first pass — they are the most automatable human-facing checklist in this report, because they are
stated as inspectable properties rather than outcomes.

### 2.3 Exploratory testing and session-based test management (Bach)

SBTM is the format for "go look for trouble, but be accountable about it". Components: a
**charter** (the mission of the session, giving direction without prescribing actions); a
time-boxed **session**; a **session sheet** whose TASK BREAKDOWN records duration split into
T (test design + execution), B (bug investigation + reporting), S (setup), plus opportunity and
idle time; and a **debrief** structured by Jonathan Bach's **PROOF** — Past (what happened),
Results (what was achieved), Obstacles (what got in the way), Outlook (what still needs doing),
Feelings (how the tester feels about it)
(<https://en.wikipedia.org/wiki/Session-based_testing>; the seminal article is behind a download
landing page at <https://www.satisfice.com/download/session-based-test-management> which I could
not read — the PROOF and TBS details above come from the encyclopaedia entry and practitioner
summaries, not from Bach's paper directly).

**Why this matters for the feature:** SBTM is the existing, field-proven answer to "how do you get
a recordable QA artefact out of unstructured human poking?" It is a *charter + a timebox + a
debrief form* — three fields, not a test script. It is the natural shape for the owner's
"Human QA" section on a card where there is no scripted scenario to stage.

### 2.4 Dogfooding, staged rollout, beta channels

Chrome is the reference implementation: Canary (daily, early-stage features), Dev (1–2×/week),
Beta (weekly, ~4-week majors, features reach beta users "over a month before Stable"), Stable
(every 2–4 weeks, after "rigorous automated testing and manual checks"), and then a **staged
rollout starting at 1–5% of users and building to 100%**, with metric monitoring and the ability
to pause (<https://developer.chrome.com/docs/web-platform/chrome-release-channels>).

The structural insight: exposure is itself a QA instrument. A change that cannot be judged before
release is judged *by* release, in a shape where the judgement is reversible.

### 2.5 A/B tests, OEC and guardrail metrics

Microsoft's ExP framing: the hard part is choosing the **OEC (Overall Evaluation Criterion)** —
"the criterion by which to evaluate the different variants" — and the trap is that "short-term
changes to metrics may not predict the long-term impact of a change" ("Pitfalls of Long-Term Online
Controlled Experiments", Dmitriev, Frasca, Gupta, Kohavi, Vaz, IEEE Big Data 2016). Bing routinely
sees experiments worth millions of dollars a year in either direction, and the published value of
the practice is as much in "negative features that we avoided deploying, despite key stakeholders'
early excitement" ("Online Controlled Experiments at Large Scale", Kohavi et al.)
(<https://exp-platform.com/>). I could **not** confirm the term "guardrail metric" on that site;
it is standard usage in the field (metrics that must not regress, e.g. latency, crash rate,
unsubscribes) but I have no primary citation for it here.

### 2.6 UAT in enterprise software

I could not land a primary source (see unverified). What is uncontroversial in practice and
consistent with the Scrum Guide's Definition of Done below: UAT is run by *business* users, not the
build team, from a **UAT script** — a numbered table of (Step, Action, Test data, Expected result,
Actual result, Pass/Fail, Tester, Date, Comments) — and it terminates in a named person's
**sign-off**. Distinguishing features versus a usability test: UAT asks "does this do what we
contracted for", is written as steps *with expected results*, and its output is an authorisation,
not an insight. Treat the format as borrowable; treat my field list as unverified.

### 2.7 Design QA and visual regression

- **Percy**: snapshots across browsers and widths, compared against an approved **baseline**;
  reviewers "review changes, request changes, and approve them before merging". The docs are
  explicit that the tool does not decide: not all visual changes are bugs, and a human separates
  intentional design updates from regressions
  (<https://www.browserstack.com/docs/percy/overview/visual-testing-basics>).
- **Chromatic**: visual tests ("pinpoint changes in appearance, layout, fonts, and colors"),
  interaction tests, axe-based accessibility tests, and a **UI Review** step that lets you "assign
  reviewers and resolve discussions to streamline team sign-off"
  (<https://www.chromatic.com/docs/>).

**The shape to steal:** the machine's job is *"here is exactly what changed visually"*; the human's
job is a **two-valued decision on a diff** — intended / not intended. That is the cheapest possible
human QA artefact and it is what most UI work actually needs. It is not a scenario at all.

### 2.8 Accessibility: the clearest measured split between machine and human

- W3C WAI states it flatly: "no tool alone can determine if a site meets accessibility standards.
  Knowledgeable human evaluation is required to determine if a site is accessible."
  (<https://www.w3.org/WAI/test-evaluate/>)
- **Measured coverage, by success criterion:** ~30% of WCAG 2.1 AA success criteria (about 15–16 of
  50) are meaningfully machine-testable.
- **Measured coverage, by issue volume:** Deque reports axe-core surfacing ~57% of issues *by
  count* in its own dataset, because the machine-catchable failures (missing alt text, contrast,
  unlabelled fields) are also the most numerous
  (<https://www.deque.com/blog/automated-testing-study-identifies-57-percent-of-digital-accessibility-issues/>).
- **Independent head-to-head:** GDS seeded one page with **142 known barriers** and ran **13**
  automated checkers. Best tool: **40%** on a definitive pass/fail basis (50% if you count items it
  merely flagged for human review)
  (<https://alphagov.github.io/accessibility-tool-audit/>).

Both framings are true and the difference between "30%" and "57%" is entirely *what you count*.
That is a directly reusable lesson for a QA-shape feature: **a coverage number means nothing
without saying whether it counts rules or defects.**

### 2.9 Automated pre-launch crawling (the closest existing thing to "an AI plays the scenario first")

Google Play's **pre-launch report** runs a robo crawler over a real app on real devices in
several languages before release, and returns: crashes/ANRs, defective libraries, unsupported APIs,
CPU/network/memory/frame-rate graphs, an accessibility scan (content labelling, touch target size,
layout, contrast), and **screenshots and videos** per device, with issues grouped by severity —
plus the explicit caveat that "Google can't guarantee that tests will identify all issues"
(<https://support.google.com/googleplay/android-developer/answer/9844487>).

This is the existing industry precedent for the owner's "an AI plays the scenario first": a machine
drives the product, and what it hands the human is *evidence* (screenshots, video, a severity-sorted
list), not a verdict.

---

## 3. Low-UX tools: CLIs, libraries, compilers, formatters, linters

Here the oracle is usually mechanical, and the human is left with a small, specific residue.

### 3.1 Differential testing

Csmith: "a random generator of C programs. Its primary purpose is to find compiler bugs with random
programs, **using differential testing as the test oracle**". Generate a UB-free program, compile
with GCC and Clang, run both, compare output; a divergence means one of them is wrong
(<https://github.com/csmith-project/csmith>). No human in the loop at all until triage.

### 3.2 Conformance suites

test262 is the ECMAScript conformance suite: **50,000+ test files** across ECMA-262/402/404, and a
TC39 proposal cannot reach Stage 4 without tests. Only tests flagged **REQUIRED** must pass; other
failures are signal for a human to interpret (<https://github.com/tc39/test262>). Chromium's web
platform feature process requires Web Platform Tests coverage as a named artefact
(<https://www.chromium.org/blink/launching-features/>).

### 3.3 Property-based testing and fuzzing

Hypothesis's own definition is the sharp one: property-based testing is "the construction of tests
such that, when these tests are fuzzed, failures in the test reveal problems with the system under
test **that could not have been revealed by direct fuzzing of that system**." The worked example is
directly relevant to a formatter: run it over a corpus of Python files and assert the output
satisfies PEP 8 — a behavioural contract beyond "it didn't crash"
(<https://hypothesis.works/articles/what-is-property-based-testing/>). The human contribution is
**choosing the property**; the machine does everything after that.

### 3.4 Real-world corpora: the industry's answer to "does this break anyone?"

- **Crater** (Rust): compiles and tests **every crate on crates.io** (plus GitHub repos) twice,
  once on a "start" toolchain and once on "end"; passes-then-fails is reported as a regression.
  Modes: `build-and-test`, `build-only`, `check-only`, `clippy`, `rustdoc`, `fix`. Driven by
  `@craterbot run` comments on the PR, results as an HTML report with logs, run by the release team
  during every beta cycle (<https://github.com/rust-lang/crater>,
  <https://raw.githubusercontent.com/rust-lang/crater/master/docs/bot-usage.md>,
  <https://doc.crates.io/contrib/tests/crater.html>).
- **diff-shades** (Black): runs Black across a list of open-source projects and **compares two
  revisions of Black** so you can see the exact formatting change a PR causes; CI posts
  "diff-shades results comparing …" on the PR, with an `assert-no-changes` job that fails on any
  change to the stable style. Black's contributor docs then hand the *judgement* to a person:
  "think about if the change seems disruptive enough to cause frustration to projects that are
  already 'Black-formatted'"
  (<https://black.readthedocs.io/en/stable/contributing/gauging_changes.html>,
  <https://github.com/ichard26/diff-shades>).

**This is the best template in the whole report for a punctuation-fixing CLI.** The machine runs
the tool over a large real corpus and produces the *complete, exact diff of behaviour change*. The
human answers one question: **is this amount of churn worth it?** That is a judgement, it is not
automatable, and it takes thirty seconds — nothing like a staged scenario.

### 3.5 API compatibility / semver

Cargo's SemVer reference classifies changes as major / minor / possibly-breaking, with worked
examples (removing a public item = major; adding a private field to an all-public struct = major
because struct literals break; adding an enum variant without `#[non_exhaustive]` = major because
exhaustive `match` breaks) (<https://doc.rust-lang.org/cargo/reference/semver.html>). Notably the
page itself describes a **review process against guidelines**, not automated verification —
though the "possibly-breaking" category is explicitly "maintainers must decide severity". Tools
exist in the ecosystem (cargo-semver-checks, japicmp, Go's `apidiff`) but the residual judgement —
*is this break acceptable?* — is human by construction.

### 3.6 Docs as tests

doctest: executable examples living in docstrings, with three stated uses — checking that a
module's docstrings are up to date, regression testing from example files, and "executable
documentation / literate testing". The docs' own confession: "I'm still amazed at how often one of
my doctest examples stops working after a 'harmless' change"
(<https://docs.python.org/3/library/doctest.html>). Machine-checkable that the example *runs*;
not checkable that the example is the right example to show.

### 3.7 What is still human in a zero-UX tool

rustc's diagnostics guide is the cleanest published statement of a human-only quality bar in a
compiler. Automated: UI tests with stderr snapshots, which pin the exact bytes of every message.
Human: whether the message is any good. The style guide is a list of judgements — messages "start
with a lowercase letter and do not end with punctuation"; "matter of fact"; "plain simple English";
avoid "illegal", prefer "invalid"; keep it "succinct" because users see it repeatedly; reduce spans
"to the smallest amount possible that still signifies the issue"; and remember "Rust's learning
curve is rather steep, and the compiler messages are an important learning tool". Its escape hatch
is explicit: "**if you are not sure, just ask your reviewer!**"
(<https://rustc-dev-guide.rust-lang.org/diagnostics.html>).

So for a CLI/compiler/library the human residue is a short, stable list:
**error and help text wording; default values; what the tool does when the user is wrong; whether
the docs example is the right example; whether the amount of churn is acceptable.** None of these
needs a staged scenario. All of them need someone to *read the output*.

---

## 4. Mixed projects: deciding which changes need human QA at all

This is the part of the field that maps most directly onto the owner's problem.

### 4.1 Change classification at the PR gate (the best borrowable mechanism)

Kubernetes forces every PR to answer **"Does this PR introduce a user-facing change?"** in a
release-note block. Rules: "any user-visible or operator-visible change qualifies for a release
note", including CLI changes, API changes, behavioural changes, deprecations and vulnerability
fixes; internal tests and build infrastructure are excluded and use `/release-note-none` or the
literal `NONE`. If you leave it blank, `do-not-merge/release-note-label-needed` is applied
**automatically on PR creation** and blocks the merge until you answer
(<https://raw.githubusercontent.com/kubernetes/community/master/contributors/guide/release-notes.md>).

That is exactly the gate the QA-shape feature needs, and it already exists in a battle-tested form:
a **mandatory, blocking, one-question classification of user-visibility**, with a cheap "none"
answer. The machine cannot decide it, but it can *demand* it and pre-fill a guess.

### 4.2 Launch gates in a browser vendor

Chromium's web-platform feature launch: an **explainer**, a **spec**, **WPT coverage**, a
**ChromeStatus entry**, an optional **origin trial**, a **TAG review** ≥1 month before shipping,
and finally an **Intent to Ship** needing **3 LGTMs from the API owners**. The machine enforces
runtime flags, test automation and workflow progression; the humans judge site-breakage risk,
adequacy of vendor coordination, spec maturity, community consensus, alignment with web platform
values, and whether readiness claims are backed by evidence
(<https://www.chromium.org/blink/launching-features/>).

Worth noting what the humans judge there: **almost all of it is risk and consensus, none of it is
"does the button work".**

### 4.3 Risk-based test selection, mechanised

- **Microsoft's L0–L4 taxonomy** classifies each test by dependency and runtime, and then binds
  the classes to points in the pipeline: L0/L1 unit tests (<60 ms and <400 ms average
  respectively, no test over 2 s), L2 functional-with-dependencies, L3 against a testable service
  deployment, L4 integration against production. "Developers always run through L2 tests before
  committing, a pull request automatically fails if the L3 test run fails, and the deployment might
  be blocked if L4 tests fail." Also: "Discourage the use of UI tests, because they tend to be
  unreliable."
  (<https://learn.microsoft.com/en-us/devops/develop/shift-left-make-testing-fast-reliable>)
- **Facebook's predictive test selection** replaces "which tests *could* be affected" with "what is
  the *likelihood* this test finds a regression", using a gradient-boosted tree over historical
  change/outcome pairs: **catches 99.9% of regressions while running one third of the transitively
  dependent tests**, with 95%+ accuracy in predicting outcomes
  (<https://engineering.fb.com/2018/11/21/developer-tools/predictive-test-selection/>).

The transferable idea: **risk is predicted from the change itself, from history, not declared by
the author.** A QA-shape recommender could plausibly be trained the same way — "cards touching
these files historically needed a human".

### 4.4 Definition of Done

Scrum's: "The Definition of Done is a formal description of the state of the Increment when it
meets the quality measures required for the product", and an item that falls short "cannot be
released or presented at the Sprint Review" — it returns to the backlog
(<https://scrumguides.org/scrum-guide.html>). A per-card QA shape is, formally, a per-card
Definition of Done.

### 4.5 Acceptance criteria as Given/When/Then, and who actually reads them

Gherkin's keywords: `Feature`, `Rule`, `Example`/`Scenario`, `Given`/`When`/`Then`/`And`/`But`,
`Background`, `Scenario Outline`, `Examples`; plus doc strings, data tables, tags and comments.
The canonical example (<https://cucumber.io/docs/gherkin/reference>):

```gherkin
Feature: Guess the word

Scenario: Maker starts a game
  When the Maker starts a game
  Then the Maker waits for a Breaker to join

Scenario: Breaker joins a game
  Given the Maker has started a game with the word "silky"
  When the Breaker joins the Maker's game
  Then the Breaker must guess a word with 5 characters
```

On audience and style, Cucumber's own guidance is that scenarios are **living documentation** and
must be **declarative, not imperative** (<https://cucumber.io/docs/bdd/better-gherkin/>):

- Imperative (discouraged): "When I type 'x@y.com' in the email field / And I type
  'validPassword123' in the password field / And I press the 'Submit' button"
- Declarative (recommended): "When Free Frieda logs in with her valid credentials / Then she sees
  a Free article"

Rationale given: imperative scenarios are "so closely tied to the mechanics of the current UI" that
"any time the implementation changes, the tests need to be updated too", whereas declarative ones
survive product changes and "read better as living documentation".

**Note the convergence with NN/g.** A good Gherkin scenario and a good usability task scenario
follow the *same* rule from opposite directions: say the situation and the goal, never the clicks.
That is a strong signal that the owner's "staged test case" should be written once, declaratively,
and consumed two ways — as an automatable acceptance test *and* as the brief a human is handed.

---

## 5. How agentic coding products hand work to a human today

Short version: **the state of the art is "a diff, a log, and a link" — almost nobody stages a
scenario or seeds data for the reviewer.** That is the gap the owner's feature would be filling.

| Product | What the human is handed | Scenario/seed staging? |
|---|---|---|
| **GitHub Copilot coding agent** | A PR; "every step happening in a commit and being viewable in logs"; the human "review[s] the diff, iterate[s], and create[s] a pull request when you're ready" (<https://docs.github.com/en/copilot/concepts/agents/coding-agent/about-coding-agent>). The **Playwright MCP server is enabled by default** alongside the GitHub MCP server, so the agent can drive a browser — but the docs I read do not state what it uses it for or that screenshots land in the PR (<https://docs.github.com/en/copilot/how-tos/use-copilot-agents/coding-agent/extend-coding-agent-with-mcp>) | No |
| **OpenAI Codex cloud** | "the summary and diff"; task logs; then "open a pull request when the result is ready" (<https://learn.chatgpt.com/docs/cloud>) | No |
| **Devin** | PR, plus a live **embedded IDE with Shell / IDE / Browser tabs** you can take over; guidance explicitly says good tasks are ones "easy to verify — e.g. checking that CI passes or testing an automatic deployment" (<https://docs.devin.ai/get-started/devin-intro>) | No — it pushes verification back onto CI/deploys |
| **Google Jules** | A **plan you approve before any code changes**, then notifications on completion (<https://jules.google/docs>) | Approval is *pre*-work, not acceptance of the result |
| **Replit Agent** | "Agent tests its own work on a regular basis" and "creates checkpoints as it works, so you can roll back to any previous state"; otherwise chat-based repair (<https://docs.replit.com/replitai/agent>) | Checkpoint/rollback, not a staged scenario |
| **Lovable** | In-project **live preview**, then "publish it to a live URL", then optional Git sync (<https://docs.lovable.dev/introduction>) | No |
| **Preview deployments (Vercel, the substrate under most of these)** | A per-PR deployment with branch- and commit-specific URLs; "you'll typically see links appear in your Git provider's PR comments"; plus staged production deployments held for manual promotion (<https://vercel.com/docs/deployments/environments>) | Environment is staged; the *scenario* is not |

The two products that *do* stage something for the reviewer are not coding agents at all:

- **Google Play pre-launch report** — drives the real app on real devices and returns screenshots,
  video, crash traces and a severity-ranked issue list (§2.9).
- **Percy / Chromatic** — pre-compute the exact visual delta so the human's task collapses to
  accept/deny on a diff (§2.7).

Both make the same move: **the machine's output is evidence positioned for a decision, and the
decision is small.** That is a much better target than "here is a checklist, go test it".

---

## (a) Kind of change → judge → QA artefact → AI first pass → left to a person

| # | Kind of change | Who/what is the judge | QA artefact that fits | What an AI can do first | What is left to a person |
|---|---|---|---|---|---|
| 1 | Platformer jump/movement feel tuning | A player's hands; the designer watching | **Playtest question sheet** + an observed session under Valve's rules (no hints, no answers) | Measure apex/airtime/coyote frames; bot-verify every jump in the level is still completable; produce a before/after side-by-side recording | Whether it feels heavy/floaty/snappy; whether players now attempt the jump; whether the section's difficulty curve reads right |
| 2 | Fighting-game input latency or netcode change | A measuring rig, then a high-skill player | Latency budget in ms/frames + a **replay-based regression suite** (recorded inputs replayed deterministically) | Measure end-to-end latency; replay the input corpus and diff the resulting game states frame-by-frame | Whether the change is *perceptible*; whether combos still "link" at the level of feel |
| 3 | Rhythm game audio/visual sync or calibration | A player with headphones | Playtest with a **calibration task scenario** | Measure audio-to-frame offset across devices; assert offset stays in a stated window | Whether it feels on-beat; whether the calibration UI is usable in a dark room |
| 4 | Level layout / pacing / difficulty curve | Fresh ("kleenex") players, observed | **Whole-playthrough playtest**; telemetry heatmap of deaths/drop-off | Bot playthrough for reachability and soft-locks; heatmap of where a bot dies | Valve, verbatim: pacing, difficulty curve, variety, chapter-to-chapter inconsistency — "can't be measured until it's all there" |
| 5 | Game content bug (clipping, soft-lock, missing collider) | A test case; anyone | Scripted **QA test case** with reproduction steps | Reproduce, capture video, assert fixed; regression-test with a bot | Nothing, once the repro is scripted |
| 6 | New onboarding / first-run flow in an app | **Five fresh users**, think-aloud | **Usability task scenario** (situation + goal, never steps) + observed session | Walk the flow in a browser, screenshot each step, flag heuristic violations (Nielsen's 10), check every control is reachable and labelled | Whether a person who has never seen it can get through without help; where they hesitate; what they expected to happen |
| 7 | Power-user flow in a productivity tool (new shortcut, new command) | The maintainers themselves, in daily use | **Dogfood period** + an **exploratory test charter** with a PROOF debrief | Verify the binding fires, doesn't collide with existing bindings, appears in help; drive it headlessly | Whether it's the right binding; whether it's discoverable; whether it's worth the keymap slot |
| 8 | Restyle / layout change to an existing component | A designer, on a diff | **Visual diff, accept/deny** (Percy/Chromatic baseline) | Render every state × width × theme and produce the pixel diff; run axe | One binary call per diff: intended or regression |
| 9 | Accessibility change (focus order, ARIA, contrast) | A screen-reader user, plus an auditor | **WCAG audit** with per-criterion findings; AT walkthrough | Run axe/SortSite-class checks — 30% of AA criteria, ~40–57% of real defects depending on counting | The other 60–70%: does the focus order make sense, are the labels *meaningful*, is the announcement comprehensible |
| 10 | Enterprise workflow change (approval routing, permissions) | The business owner of the process | **UAT script** (steps + expected result + pass/fail + named sign-off) | Execute every step against seeded fixture data and record actual vs expected | Whether "expected result" is what the business actually wants; the signature |
| 11 | Ranking / recommendation / growth change | The population, via an experiment | **A/B test** with a declared OEC + guardrail metrics | Build the experiment, check SRM, compute metrics and CIs, monitor guardrails | Choosing the OEC; deciding whether a short-term win is a long-term loss; whether to ship at all |
| 12 | Risky change with no pre-release oracle | Production, slowly | **Staged rollout** 1% → 100% with kill switch (Chrome's shape) | Gate, ramp, watch crash/latency/engagement, auto-halt on regression | Deciding what counts as "bad enough to stop" |
| 13 | CLI error message / `--help` text wording | A reviewer reading it | **A written style guide + reviewer sign-off** (rustc's "if you are not sure, just ask your reviewer!") | Snapshot the exact stderr bytes (UI tests) so it can never change silently; check it against the mechanical rules (lowercase, no trailing period, backticked identifiers) | Is the message actually helpful? Does it teach? Is "invalid" the right word? |
| 14 | CLI flag semantics / exit code / default value | A spec, then a maintainer | **Golden/snapshot tests** + a documented compatibility note | Assert exit codes and output for a matrix of invocations; diff against the previous release's behaviour | Is this the right default? Is the break worth it? |
| 15 | **Punctuation-fixing CLI: a change to the fixing rule** | A corpus, then a maintainer glancing at the churn | **Corpus differential run** — the diff-shades/Crater shape | Run old vs new over thousands of real files; produce the complete, exact set of changed outputs, counted and sampled; assert idempotence and round-trip properties | One question: *is this amount of churn acceptable, and are the sampled changes what we meant?* (~30 seconds) |
| 16 | Formatter / linter new rule | Same, plus the ecosystem | Corpus differential + a stability assertion (`assert-no-changes`) | Everything above, plus false-positive rate on the corpus | Whether the rule's *advice* is good advice |
| 17 | Library public API change | The semver rules, then a maintainer | **Semver classification** + a real-world compile run (Crater) | Classify against the semver reference; compile every downstream consumer and report pass→fail | Is this break acceptable? Is the new API name right? |
| 18 | Compiler optimisation / codegen pass | A differential oracle | **Differential testing** (Csmith-style) + conformance suite | Generate programs, compile with both, diff outputs; run test262/WPT-class suites; measure compile time and binary size | Nothing, until triage — then: is this miscompile or is the test wrong? |
| 19 | Protocol / file-format parser change | A conformance suite | **Conformance suite** with REQUIRED-flagged tests + fuzzing | Run the suite; fuzz; property-test round-trip and idempotence | Interpreting non-REQUIRED failures; deciding on spec ambiguity |
| 20 | Pure refactor, internal perf, build/infra | The test suite and the clock | **Test-impact-selected suite** + a benchmark | Select the tests most likely to catch a regression (predictive selection); run benchmarks with thresholds | Nothing — this is the "release-note-none" class and must be cheap to declare |

**How to read the table.** Rows 1–4 and 6 need a *staged scenario with a fresh person*; rows 8 and
15–16 need a *diff and a two-second decision*; rows 13–14 and 17 need *someone to read the output*;
rows 5, 18–20 need no human at all. Four distinct human-QA shapes, not one. The owner's "human is
put into a staged test case" is shape #1 — correct, but it is roughly a quarter of the field.

---

## (b) Terms and formats worth borrowing verbatim

1. **Usability task scenario** — a situation plus a goal, never the steps.
   *Real example (NN/g):* "Buy a pair of shoes for less than $40." (bad version: "Purchase a pair
   of orange Nike running shoes"). <https://www.nngroup.com/articles/task-scenarios-usability-testing/>

2. **Exploratory test charter** — a mission for a timeboxed session, giving direction without
   prescribing actions. *Real example (SBTM convention):* "Analyse the CSV import feature; explore
   malformed and very large files; report anything that loses data." Paired with a **session
   sheet** whose TASK BREAKDOWN splits the timebox into T (design+execution) / B (bug
   investigation) / S (setup). <https://en.wikipedia.org/wiki/Session-based_testing>

3. **PROOF debrief** — the five debrief prompts: Past, Results, Obstacles, Outlook, Feelings.
   *Real example:* "Outlook: the Unicode path is untested; Feelings: I don't trust the error
   handling." Same source. **This is the best ready-made format for a "Human QA" section that has
   no script**: five short fields, and "Feelings" legitimises the judgement the owner actually
   wants.

4. **Given / When / Then (declarative)** — acceptance criteria written as living documentation.
   *Real example (Cucumber):* `Given the Maker has started a game with the word "silky" / When the
   Breaker joins the Maker's game / Then the Breaker must guess a word with 5 characters`.
   <https://cucumber.io/docs/gherkin/reference>

5. **Kleenex tester** — someone brought in once, told nothing in advance, never asked back; used
   for one aspect (UI, tutorial, one feature), not the whole product.
   <https://www.tumblr.com/askagamedev/701096843155734528/how-often-are-playtesters-used-in-game>

6. **"Simulate the player in their living room"** — the facilitator rule: don't give hints, don't
   answer questions, don't provide extrinsic rewards. Verbatim from Valve's GDC 2006 deck.
   <https://www.gamedevs.org/uploads/valve-design-process-for-creating-half-life2.ppt>

7. **"You learn more from what playtesters *don't* experience"** — the observation prompt that
   turns a playtest into coverage data. Same deck. Directly applicable: an AI running the scenario
   should report what the user *never reached*, not just what failed.

8. **Baseline / accept / deny** — the visual-regression decision. *Real example (Percy):* reviewers
   "review changes, request changes, and approve them before merging"; the tool never decides
   whether a change is a bug.
   <https://www.browserstack.com/docs/percy/overview/visual-testing-basics>

9. **Release-note block** — a blocking, mandatory user-visibility classification on every change.
   *Real example (Kubernetes PR template):* "Does this PR introduce a user-facing change?" with
   `NONE` / `/release-note-none` as the cheap answer and
   `do-not-merge/release-note-label-needed` auto-applied if you skip it.
   <https://raw.githubusercontent.com/kubernetes/community/master/contributors/guide/release-notes.md>

10. **UAT script + sign-off** — numbered steps with Test data / Expected result / Actual result /
    Pass-Fail / Tester / Date, terminating in a named person's authorisation. *Format unverified
    against a primary source — see below.*

11. **OEC + guardrail metrics** — name the single criterion the change is judged on, and the
    metrics that merely must not regress. *Real example (ExP):* the OEC problem is "to select an
    Overall Evaluation Criterion … by which to evaluate the different variants", and the trap is
    that short-term metrics may not predict long-term impact. <https://exp-platform.com/>

12. **Corpus differential run** — old vs new over a large real corpus, reporting the exact
    behaviour delta. *Real examples:* `@craterbot run start=stable end=beta mode=build-and-test`
    over all of crates.io (<https://github.com/rust-lang/crater>), and Black's "diff-shades results
    comparing …" PR comment, where the human question is "is the change disruptive enough to cause
    frustration to projects that are already Black-formatted"
    (<https://black.readthedocs.io/en/stable/contributing/gauging_changes.html>).

13. **Heuristic evaluation against Nielsen's 10** — an expert inspection when there are no users to
    recruit. *Real example heuristic:* "Visibility of system status."
    <https://www.nngroup.com/articles/ten-usability-heuristics/>

14. **Pre-launch report** — machine drives the real product, hands back screenshots, video,
    severity-ranked findings, and an explicit "this doesn't catch everything".
    <https://support.google.com/googleplay/android-developer/answer/9844487>

15. **"Defies direct analysis"** — Valve's own name for the class of property that forces human QA.
    Worth using verbatim as the *test* a card is put to when picking its QA shape.

---

## What I could not verify

- **Steve Swink's metrics list, from the primary text.** The GDC PDF
  (<https://dtc-wsuv.org/wp/dtc338-engines/files/2017/01/Mechanics-and-Metrics-of-Game-Feel-Steve-Swink.pdf>)
  exceeded the 10 MB fetch limit. The definition and the "tight/floaty" metrics framing come from
  <https://en.wikipedia.org/wiki/Game_feel> and search summaries, not from Swink directly.
  "Designing Game Feel: A Survey" (<https://arxiv.org/pdf/2011.09201>) appeared in search results
  and looks like a good next read; I did not read it.
- **"Over 100 playtesters per level" for Half-Life 2.** Widely repeated, and it did *not* appear in
  the slide text I extracted. Treat as unconfirmed.
- **Nintendo's playtesting protocol.** I verified only Mario Club's existence, founding date,
  headcount and remit. Miyamoto's "show it to someone who doesn't play games" practice is
  well-known anecdotally; I have no primary citation and did not include it as fact.
- **Telemetry heatmaps.** No primary source obtained (Bungie/Valve GDC material, or Microsoft
  Research's TRUE instrumentation paper — its MSR URL 404'd). The claim in §1.4 that heatmaps
  *locate* rather than *judge* is my inference from the rest, not a cited finding.
- **Bug bashes.** WebSearch budget was exhausted and the Wikipedia redirect goes to a general
  "debugging" article with no bug-bash content. I have no primary source and have deliberately not
  described the practice in detail.
- **Chromium's `Test-Manual` label and Firefox's `qe-verify` Bugzilla flag.** Both are real to the
  best of my knowledge and are exactly the "which bugs need a human to verify" mechanism the owner
  is after, but I could not fetch either. §4.1's Kubernetes release-note gate is the verified
  substitute and is arguably a better model anyway.
- **UAT script format.** No primary source landed (the Atlassian page 404'd; ISTQB glossary not
  fetched). The field list in §2.6 and item 10 above is conventional practice as I know it, not a
  citation.
- **"Guardrail metric" as a term at Microsoft ExP.** The concept (OEC vs metrics that must not
  regress) is sourced; the specific phrase is not, on that site.
- **Kohavi's "about a third of ideas improve the metric they were designed to improve."** Widely
  cited; I could not retrieve it from a primary page in this session, so it is not stated as fact
  above.
- **Copilot coding agent posting screenshots in the PR.** Verified only that the Playwright MCP
  server is on by default; the docs I read do not say what it is used for or that images land in
  the PR. The GitHub changelog URL I tried 404'd.
- **Lovable, v0, Cursor background agents, Jules** — thin. Lovable's intro page covers
  preview/publish/Git-sync only; Cursor and OpenAI docs both redirected to index pages; Jules' docs
  page covers the plan-approval step but not the result-review step. None of them appears to seed
  data or stage a scenario, but "appears" is the right word.
