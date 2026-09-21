# What decides the shape of QA: oracles, standards, taxonomies, and a per-card classifier

Research slice D — cross-cutting theory and existing taxonomies. 2026-09-21.

**Method / caveats.** The session's WebSearch budget was already spent when this task started, so
every source below was fetched directly by URL (`curl` + `pdftotext`, or WebFetch) and read, rather
than found by search. That biases the set towards sources I could name from memory and then verify;
it is not a systematic literature search. Everything asserted below is from a page or PDF I actually
read, except where the text says "could not verify". Nothing was edited or committed in the repo.

---

## 1. The test oracle problem

**The survey.** Earl T. Barr, Mark Harman, Phil McMinn, Muzammil Shahbaz, Shin Yoo, "The Oracle
Problem in Software Testing: A Survey", *IEEE Transactions on Software Engineering* 41(5), 507–525,
2015. DOI [10.1109/TSE.2014.2372785](https://doi.org/10.1109/TSE.2014.2372785); full text read at
<https://coinse.github.io/publications/pdfs/Barr2015qd.pdf>.

The framing sentence is the one that matters for a Kanban tool: *"Given an input for a system, the
challenge of distinguishing the corresponding desired, correct behaviour from potentially incorrect
behavior is called the 'test oracle problem'. … Without test oracle automation, the human has to
determine whether observed behaviour is correct."* The oracle problem **is** the question "who
judges this card".

They classify 694 publications into exactly four buckets, and these four are the spine of the
proposal in §7:

| Category | What it is (survey's words) | Count in survey |
|---|---|---|
| **Specified** | oracles that "judge all behavioural aspects of a system with respect to a given formal specification" — model-based (Z, B, UML/OCL, VDM, Alloy, Larch), algebraic, assertions, contracts | 317 |
| **Derived** | oracles derived from some other artefact — "for instance, a previous version of the system": regression/golden output, pseudo-oracles, N-version, metamorphic relations, specification mining, documentation | 245 |
| **Implicit** | "one that relies on general, implicit knowledge to distinguish between a system's correct and incorrect behaviour … such facts as 'buffer overflows and segfaults are nearly always errors'. … requires neither domain knowledge nor a formal specification … applies to nearly all programs" | 76 |
| **No automated oracle** (the human oracle) | "no such artefact exists so a human tester must verify whether software behaviour is correct given some stimuli" | 56 |

Three details are load-bearing for a per-card classifier:

- **Implicit oracles are not universal, and they are weak by construction.** The survey is explicit:
  *"Behaviours abnormal for one system in one context may be normal for that system in a different
  context."* An implicit oracle detects "blatant faults"; it never tells you the thing is *right*.
  A card whose only oracle is implicit ("it doesn't crash") has, formally, no oracle for its claim.
- **Pseudo-oracles and differential testing.** The pseudo-oracle is Davis & Weyuker's answer to
  *non-testable programs*, defined as *"Programs which were written in order to determine the answer
  in the first place. There would be no need to write such programs, if the correct answer were
  known."* A pseudo-oracle is "an alternative version of the program produced independently, e.g. by
  a different programming team or written in an entirely different programming language"; N-version
  programming generalises it to k implementations plus a voting threshold. This is the direct
  warrant for "check the new estimator against the published package" and for "diff against the
  previous release".
- **Metamorphic relations.** *"For the SUT p that implements the function f, a metamorphic relation
  is a relation over applications of f that we expect to hold across multiple executions of p."* The
  survey places MT under *derived* rather than *specified* oracles, "because, in practice,
  metamorphic relations are usually manually inferred from a white-box inspection of a SUT" — i.e.
  **the relations themselves are a human design act even though running them is mechanical.** That
  split (human designs the relation, machine runs it) is exactly the split a board needs between
  "planning/acceptance" and "QA checklist".

**Metamorphic testing survey.** Sergio Segura, Gordon Fraser, Ana B. Sánchez, Antonio Ruiz-Cortés,
"A Survey on Metamorphic Testing", *IEEE TSE* 42(9), 805–824, 2016. DOI
[10.1109/TSE.2016.2532875](https://doi.org/10.1109/TSE.2016.2532875); full text read at
<https://eprints.whiterose.ac.uk/id/eprint/110335/>. 119 papers, 1998–2015. Its canonical example is
the one to quote at anyone who says "there's no way to test this": *"What is the exact value of
sin(12)? Is an observed output of −0.5365 correct? … sin(x) = sin(π − x), and we can use this to
test whether sin(12) = sin(π − 12) without knowing the concrete values of either sine calculation."*

Application domains it counts: web services and applications 16%, computer graphics 12%, simulation
and modelling 12%, embedded systems 10%, "other" (financial software, optimisation, encryption) 21%;
numerical programs only 4%, "even though this seems to be the dominant domain used to illustrate
metamorphic testing". Machine learning gets its own section: Murphy et al.'s six relations believed
to hold in most ML applications (additive, multiplicative, permutative, invertive, inclusive,
exclusive), and Xie et al. on supervised classifiers with a distinction the proposal below reuses:
*"Violations of necessary properties are caused by faults in the algorithm and therefore are helpful
for the purpose of verification. Violations of expected properties indicate divergences between what
the algorithm does and what the user expects, and thus are helpful for the purpose of validation."*
Zhou et al. went further and defined relations on Google/Bing/Baidu "from the user perspective,
representing the properties that a user expects from a 'good' search engine". So metamorphic
relations can encode *some* user-facing expectations — but only the ones you can state as a
relation.

**Reducing human oracle cost** (Barr et al. §7). They split it two ways and this is the most
directly actionable part of the whole survey for a tool that wants to spend the owner's attention
well:

- **Quantitative** — "reduce test suite and test case size so as to maximise the benefit of each
  test case": suite minimisation under coverage, delta-debugging long generated traces, removing
  side-effect-free calls, "quick tests" fast enough to run after every compile.
- **Qualitative** — "the extent to which test cases … may be easily understood and processed by a
  human". Machine-generated inputs are drawn from a different distribution than the operational
  input profile, so *"the tester must invest time comprehending the scenario represented by test
  data in order to correctly evaluate the corresponding program output. Arbitrary inputs are much
  harder to understand than recognisable pieces of data, thus adding time to the checking process."*
  Afshan et al. used a language model in the fitness function to generate readable strings and found
  "human oracles more accurately and more quickly evaluated their test strings"; Fraser & Zeller
  mined real API usage patterns so generated scenarios "are more likely to be realistic and
  represent actual usages".
- **Crowdsourcing** (§7.3) — feasible (Pastore et al. on Mechanical Turk) but with "problems in
  presenting the test problem to the crowd such that it could be easily understood, and the need to
  provide sufficient code documentation so that the crowd could determine correct outputs".

The qualitative finding is the research justification for the owner's *staged scenario*: a human
oracle is cheaper and more accurate on a recognisable, realistic, short scenario than on an
arbitrary one. The staging is not decoration; it is the cost reduction.

---

## 2. Verification vs validation, and which qualities admit an automated judge

**IEEE 1012** (read at <https://standards.ieee.org/ieee/1012/5609/>; current edition 1012-2024,
superseding 1012-2016). Its scope sentence carries the whole distinction in one line: V&V processes
*"determine whether the development products of a given activity conform to the requirements of that
activity"* **and** *"whether the product satisfies its intended use and user needs."* Two different
questions with two different judges. Crucially: *"V&V life cycle process requirements are specified
for different integrity levels"* — IEEE 1012 is itself an assurance-level scheme (see §6), and V&V
covers "analysis, evaluation, review, inspection, assessment, and testing", not testing alone.

**ISO/IEC/IEEE 29119** (series page, <http://softwaretestingstandard.org/>): eight parts — Part 1
Concepts and Definitions (free from ISO), Part 2 Test Processes, Part 3 Test Documentation, Part 4
Test Techniques, Part 5 Keyword Driven, Part 6 Agile, Part 11 Testing of AI-Based Systems (free),
Part 13 Biometrics; supported by ISO/IEC 20246 Work Product Reviews (static testing) and ISO/IEC
33063 process assessment. Worth knowing that the context-driven school actively campaigned against
it ("Please sign the Petition to Stop ISO 29119" is a post on <https://context-driven-testing.com>),
so it is a contested rather than neutral authority.

**ISO/IEC 25010 product quality model.** The current edition has **nine** characteristics, not the
eight of the 2011 edition. Read in full at
<https://iso25000.com/index.php/en/iso-25000-standards/iso-25010> (pages 1 and 2):

Functional Suitability (completeness, correctness, appropriateness) · Performance Efficiency (time
behaviour, resource utilization, capacity) · Compatibility (co-existence, interoperability) ·
**Interaction Capability** (appropriateness recognizability, learnability, operability, user error
protection, **user engagement**, **inclusivity**, user assistance, self-descriptiveness) ·
Reliability (faultlessness, availability, fault tolerance, recoverability) · Security
(confidentiality, integrity, non-repudiation, accountability, authenticity, **resistance**) ·
Maintainability (modularity, reusability, analysability, modifiability, testability) ·
**Flexibility** (adaptability, scalability, installability, replaceability) · **Safety**
(operational constraint, risk identification, fail safe, hazard warning, safe integration).

The 2011 edition's eight were Functional Suitability, Performance Efficiency, Compatibility,
**Usability**, Reliability, Security, Maintainability, **Portability** (confirmed at
<https://en.wikipedia.org/wiki/ISO/IEC_9126>, which carries the SQuaRE history). "Usability" became
"Interaction Capability" and "Portability" was folded into "Flexibility"; Safety was added.

**Which characteristics admit an automated judge.** The standard's own wording does the sorting for
you — look for the phrase that ties the definition to a person:

| Characteristic | Definition's anchor | Automatable judge? |
|---|---|---|
| Functional Suitability | "provides accurate results **when used by intended users**"; "covers all the specified tasks and **intended users' objectives**" | correctness yes (given a spec); *completeness* and *appropriateness* need someone who knows the objectives |
| Performance Efficiency | "**meet requirements**" — response time, resource use, capacity | yes: a number against a threshold |
| Compatibility | exchange information with other products | yes |
| **Interaction Capability** | "by **specified users** … to complete specific tasks **in a variety of contexts of use**"; user engagement = "in an inviting and motivating manner"; inclusivity = "people of various backgrounds" | **no**. "Inviting and motivating" has no machine oracle. Parts (user error protection, self-descriptiveness) are checkable |
| Reliability | "performs specified functions … for a specified period" | yes: measurement over a window |
| Security | "defends against attack patterns by malicious actors" | partly: known patterns yes, novel adversary no |
| Maintainability | modularity, analysability, modifiability, testability | yes, as metrics/fitness functions (see §4) |
| Flexibility | adaptability, scalability, installability, replaceability | yes |
| Safety | fail safe, hazard warning, operational constraint | yes for the mechanisms; the hazard analysis is human |

**Quality in use.** In the 2011 edition, "quality in use" was the second model in 25010 and was
defined relative to a *context of use* (specified users, goals, environment). In the 2023 revision
the quality-in-use model was moved out of 25010 — the current iso25000.com page for 25010 shows only
the product model, and the site's navigation lists 25010, 25059, 25012, 5259, 25040 with no
quality-in-use page. **I could not verify the current quality-in-use characteristic list from a
primary source** (iso.org returns 403 to automated fetches, ANSI and iTeh likewise). My recollection
is that 25010:2011 listed effectiveness, efficiency, satisfaction, freedom from risk and context
coverage, and that the 2023 model lives in ISO/IEC 25019:2023 — **treat both as unverified.** What
*is* verified and sufficient for the proposal: the 25010 product model separates a quality defined
against a spec (functional correctness, time behaviour) from a quality defined against *specified
users in a context of use* (Interaction Capability), and only the first kind has a machine oracle.

---

## 3. The taxonomies practitioners actually use to choose

**Marick's matrix (2003) — the primary source.** Brian Marick, "My Agile testing project",
<http://www.exampler.com/old-blog/2003/08/21/>, and the seven-post series that follows. The two axes
in his words:

- *"A business-facing test is one you could describe to a business expert in terms that would (or
  should) interest her… A technology-facing test is one you describe with words drawn from the
  domain of the programmers."*
- *"whether they want the tests to **support programming** or **critique the product**. By 'support
  programming', I mean that the programmers use them as an integral part of the act of programming …
  Tests that critique the product are not focused on the act of programming. Instead, they look at a
  finished product with the intent of discovering inadequacies."*

The business-facing-product-critique post (<http://www.exampler.com/old-blog/2003/09/24.1.html>) is
the one that matters most here, and it argues the owner's case better than the owner did:

> *"The critiquers have a resource that the people creating before-the-fact examples do not: a new
> iteration of the actual working software. When you're describing something that doesn't exist yet,
> you're mentally manipulating an abstraction, an artifact of your imagination. Getting your hands on
> the product activates a different type of perception and judgment. You notice things when test
> driving a car that you do not notice when poring over its specs. Manipulation is different than
> cogitation."*

That paragraph is the theoretical justification for a `## Human QA` section existing at all, and for
its being a *staged situation someone plays* rather than a list of assertions. Marick also warns
about a failure mode the owner will hit: he tried to combine bug-finding and idea-generating in one
exploratory session and concluded *"the two goals don't go together well. Finding bugs is just too
seductive."* If the Human QA section asks both "is this broken?" and "is this the right design?",
expect only the first to get answered.

**Crispin & Gregory's Quadrants.** Lisa Crispin, "Using the Agile Testing Quadrants" (2011),
<https://lisacrispin.com/2011/11/08/using-the-agile-testing-quadrants/> (fetched via
web.archive.org). Two points she is emphatic about: *"The quadrant numbering system does NOT imply
any order"*, and *"The quadrants are merely a taxonomy to help teams plan their testing and make
sure they have all the resources they need to accomplish it. There are no hard and fast rules about
what goes in what quadrant."* — i.e. it is a **checklist against forgetting a kind of QA**, which is
precisely the job the owner wants done per card.

The canonical quadrant contents are now codified in the ISTQB Foundation syllabus v4.0.1 §5.1.7
(<https://www.istqb.org/wp-content/uploads/2024/11/ISTQB_CTFL_Syllabus_v4.0.1.pdf>, citing "Marick
2003, Crispin 2008"), which is the most quotable single source:

- **Q1** technology-facing, support the team — component and component-integration tests; *"should
  be automated and included in the CI process"*.
- **Q2** business-facing, support the team — *"functional tests, examples, user story tests, user
  experience prototypes, API testing, and simulations. These tests check the acceptance criteria and
  can be manual or automated."*
- **Q3** business-facing, critique the product — *"exploratory testing, usability testing, user
  acceptance testing. These tests are user-oriented and often manual."*
- **Q4** technology-facing, critique the product — *"smoke tests and non-functional tests (except
  usability tests). These tests are often automated."*

**This is the direct answer to the owner's complaint.** His model has been Q1+Q2 (`## Tests`) plus Q3
(`## Human QA`), with Q4 nowhere. A rate limiter, a build script, an estimator — the work with no
user — is **Q4 work**: it critiques the product and it is technology-facing, and the quadrant model
says it is "often automated" and contains no human. A card that lands in Q4 should have a QA lane
with *no human in it at all*, and that is not a gap, it is the taxonomy working.

**The pyramid and the trophy.** ISTQB §5.1.6 states the pyramid as a granularity/isolation/speed
gradient: *"The higher the layer, the lower the test granularity, the lower the test isolation …
and the higher the test execution time"*, crediting Cohn 2009 for unit/service/UI, and noting "the
number and naming of the layers may differ". Martin Fowler's "The Practical Test Pyramid"
(<https://martinfowler.com/articles/practical-test-pyramid.html>) and Kent C. Dodds' "testing
trophy" (<https://kentcdodds.com/blog/the-testing-trophy-and-testing-classifications>) argue about
the *shape* of the distribution, not about who judges — they answer "how many of each" and are
therefore orthogonal to the classification this report is after. Worth noting only so the proposal
does not get confused with them.

**Risk-based testing (ISTQB CTFL v4.0.1 §5.2).** Verified definitions:

- *"Risk is a potential event, hazard, threat, or situation whose occurrence causes an adverse
  effect"*, characterised by **risk likelihood** and **risk impact (harm)**; their combination is the
  **risk level**. Quantitatively, "risk level is calculated as the multiplication of risk likelihood
  and risk impact"; qualitatively, "using a risk matrix".
- *"The test approach, in which test activities are selected, prioritized, and managed based on risk
  analysis and risk control, is called risk-based testing."*
- **Product risks** are "related to the product quality characteristics (e.g., described in the ISO
  25010 quality model)" — missing functionality, incorrect calculations, inadequate response time,
  poor user experience, security vulnerabilities — with consequences ranging over "user
  dissatisfaction, loss of revenue, trust, reputation, damage to third parties, high maintenance
  costs, … criminal penalties, in extreme cases, physical damage, injuries or even death". **Project
  risks** are schedule/staffing/supplier and are not what a QA lane is for.
- Product risk analysis results are used to *"determine the test scope … determine the particular
  test levels and propose test types … determine the test techniques to be employed and the coverage
  to be achieved … prioritize testing"*. That is a standards body saying, in as many words, that risk
  should decide the *kind* as well as the *amount* of QA.

Two of the syllabus's seven principles are the ones to put in front of the owner:

- **#6 Testing is context dependent** — *"There is no single universally applicable approach to
  testing. Testing is done differently in different contexts (Kaner 2011)."*
- **#7 The absence-of-defects fallacy** — *"Thoroughly testing all the specified requirements and
  fixing all the defects found could still produce a system that does not fulfill the users' needs
  and expectations."* This is the reason a green `## Tests` block can never, by itself, discharge a
  card whose consumer is a person. It is verification; validation is a separate question with a
  separate judge.

**Testing vs checking — Bach & Bolton.** "Testing and Checking Refined",
<https://www.satisfice.com/blog/archives/856> (co-authored with Michael Bolton; the page is dated
2024 but this is the long-standing Rapid Software Testing formulation, originally 2013; Bolton's
earlier post is at <https://developsense.com/blog/2009/08/testing-vs-checking>). The definitions:

> *"**Testing** is the process of evaluating a product by learning about it through experiencing,
> exploring, and experimenting, which includes to some degree: questioning, study, modeling,
> observation, inference, etc."*
>
> *"**Checking** is the mechanistic process of verifying propositions about the product."*

With the explanatory notes that do the real work:

- *"'mechanistic' means algorithmic; that it can be expressed explicitly in a way that a machine
  could perform."*
- *"'propositions' are statements about a product that may be true or false. There are certain
  implications … **Quality is an opinion, not a fact, so quality cannot be verified.** But lots of
  things about a product and its behavior can be settled as a matter of fact."*
- *"Checking is a process that can, in principle be performed by a tool instead of a human, whereas
  testing can only be supported by tools. … We are not saying that a check MUST be automated. But
  the defining feature of a check is that it can be COMPLETELY automated."*
- *"A check is describable; a test might not be (that's because, unlike a check, a test involves
  tacit knowledge)."*
- *"for checking to be considered good, it must happen in the context of a competent testing
  process. **Checking is a tactic of testing.**"*

This maps onto the owner's split with almost no translation: **`## QA checklist` is checking**
(describable propositions, a tool could run them, an LLM verifier is a legitimate executor);
**`## Human QA` is testing** (tacit, exploratory, opinion-forming). And Bach & Bolton's last point
is the one to build into the workflow: a checklist is only trustworthy *downstream of* a testing act
that decided those were the right propositions. That is the argument for a human verdict being what
*generates* next time's checklist, rather than an alternative to it.

**Context-driven principles** (<https://context-driven-testing.com/>, Kaner, Bach & Pettichord). The
seven, verbatim, with the two that bear on the proposal in bold:

1. **"The value of any practice depends on its context."**
2. "There are good practices in context, but there are no best practices."
3. "People, working together, are the most important part of any project's context."
4. "Projects unfold over time in ways that are often not predictable."
5. "The product is a solution. If the problem isn't solved, the product doesn't work."
6. "Good software testing is a challenging intellectual process."
7. **"Only through judgment and skill, exercised cooperatively throughout the entire project, are we
   able to do the right things at the right times to effectively test our products."**

Plus one illustration that is directly a warning about over-fitting a classifier: *"All oracles are
fallible. Even if the product appears to pass your test, it might well have failed it in ways that
you (or the automated test program) were not monitoring."* And the airplane-vs-word-processor
example, whose punchline is *"Testing practices appropriate to the first project will fail in the
second. Practices appropriate to the second project would be criminally negligent in the first."* —
the same argument the owner is making, one level up: within one project, some cards are the airplane
and some are the word processor.

The school is also explicit that a fixed scheme is *context-aware*, not context-driven: *"when
someone looks to best practices first and to project-specific factors second, that may be
context-aware, but not context-driven."* A classifier should therefore be a **default the card can
override with a reason**, never a gate.

**Heuristic Test Strategy Model** (James Bach, v6.3, Dec 2024;
<https://www.satisfice.com/download/heuristic-test-strategy-model>). Described on its page as *"a set
of guideword heuristics designed to help you think better about test strategy. It includes four
focus areas: test techniques, project elements, product factors, and quality criteria categories …
I'd encourage you to modify this to fit the context of your own organization."* **I could not fetch
the PDF itself** (satisfice.com's download endpoint returns 500 to a direct fetch), so I have the
model's four focus areas from the abstract but not its quality-criteria guidewords. Session-Based
Test Management (Bach) is on the same site (<https://www.satisfice.com/exploratory-testing>); the
key structural idea for this brief is that exploratory work is made accountable by *charters*,
*time-boxed sessions* and a *debrief*, not by a script — which is the format the `## Human QA`
section should take.

---

## 4. Acceptance: criteria, examples, fitness functions, SLOs

**Specification by example / ATDD / BDD.** Gojko Adzic, *Specification by Example* (Manning, 2011,
ISBN 978-1617290084; <https://gojko.net/books/specification-by-example/>), from ~50 project case
studies; positioned by Dan North on that page as *"the closest I've seen to a 'BDD book' (as a
treatment of methodology)"*. The core move: agree the *examples* with the stakeholder before
implementation, and let those examples become executable specification and living documentation.
For a board, this puts "what would count as right" in **planning**, not in QA — which resolves a
confusion the owner's six cards will otherwise create (see the CLI punctuation fixer in §8: "are
these the right rules?" is an acceptance-criteria question, not a QA question, and putting it in a
QA lane wastes a human).

**ISTQB on acceptance.** Acceptance testing *"focuses on validation and on demonstrating readiness
for deployment, which means that the system fulfills the user's business needs. **Ideally, acceptance
testing should be performed by the intended users.** The main forms … are: user acceptance testing
(UAT), operational acceptance testing, contractual acceptance testing and regulatory acceptance
testing, alpha testing and beta testing."* Note the four non-UAT forms: acceptance is not always a
person's opinion — *operational* acceptance (does it deploy, back up, restore, page) and
*regulatory* acceptance are checkable.

**GQM.** Victor R. Basili, Gianluigi Caldiera, H. Dieter Rombach, "The Goal Question Metric
Approach" (<https://www.cs.umd.edu/~mvz/handouts/gqm.pdf>). Three levels: **Goal** (conceptual) —
"defined for an object, for a variety of reasons, with respect to various models of quality, **from
various points of view**, relative to a particular environment"; **Question** (operational);
**Metric** (quantitative), where data may be *objective* ("depend only on the object that is being
measured and not on the viewpoint from which they are taken") or *subjective*. The goal template is
**purpose / issue / object / viewpoint** — e.g. "Improve / the timeliness of / change request
processing / from the project manager's viewpoint."

**The `viewpoint` slot is the single most transferable idea in this whole report.** GQM says you
cannot state a quality goal without naming whose point of view it is taken from, and it says the
objective/subjective character of the metric follows from that. A card that cannot name a viewpoint
has not been planned; a card whose viewpoint is a person cannot have a purely objective metric. That
is the first question of the proposed classifier, and it is 40 years old.

**Fitness functions.** Neal Ford, Rebecca Parsons, Pat Kua, *Building Evolutionary Architectures*
(2017). Thoughtworks Technology Radar's definition
(<https://www.thoughtworks.com/en-us/radar/techniques/architectural-fitness-function>): *"Borrowed
from evolutionary computing, a fitness function is used to summarize how close a given design
solution is to achieving the set aims… An architectural fitness function … provides an objective
integrity assessment of some architectural characteristics, which may encompass existing
verification criteria, such as unit testing, metrics, monitors, and so on."* And Paul & Wang,
"Fitness function-driven development"
(<https://www.thoughtworks.com/en-us/insights/articles/fitness-function-driven-development>):
*"Architectural goals and constraints may all change independently of functional expectations.
Fitness functions describe how close an architecture is to achieving an architectural aim… with
fitness function-driven development we can also write tests that measure a system's alignment to
architectural goals."* They stress the form: *"fitness functions should describe the intent of the
'-ility'. It addresses it in terms of an objective metric that is meaningful to product teams or
stakeholders."*

This is the **automated acceptance mechanism for exactly the ISO 25010 characteristics that have no
user** — maintainability, flexibility, performance efficiency, reliability. It is the answer for
"engineering subsystems where UX barely matters": their acceptance criterion is a number with a
threshold, checked in the pipeline, not a person looking at it.

**SLOs.** Google SRE book, "Service Level Objectives"
(<https://sre.google/sre-book/service-level-objectives/>): *"It's both unrealistic and undesirable to
insist that SLOs will be met 100% of the time: doing so can reduce the rate of innovation and
deployment, require expensive, overly conservative solutions, or both. Instead, it is better to
allow an **error budget** — a rate at which the SLOs can be missed… The SLO violation rate can be
compared against the error budget, with the gap used as an input to the process that decides when to
roll out new releases."* Advice that constrains the design: "Don't pick a target based on current
performance", "Keep it simple", "Avoid absolutes".

And the SRE workbook's "Canarying Releases" chapter (<https://sre.google/workbook/canarying-releases/>):
*"We define canarying as a partial and time-limited deployment of a change in a service and its
evaluation. This evaluation helps us decide whether or not to proceed with the rollout… Canarying is
effectively an A/B testing process."* With the empirical premise: *"In Google's experience, a
majority of incidents are triggered by binary or configuration pushes."*

Taken together: for a service, **acceptance is a measured threshold plus a reversibility mechanism**,
and the last QA step is legitimately performed *in production by a machine watching a metric*, not by
a person in a staging environment. A Kanban tool that only knows "human QA" and "LLM QA" has no lane
for this, and it is the right lane for a rate limiter.

---

## 5. Evaluation where the judge must be a human — and what AI can stand in for

**Formative vs summative** (NN/g, <https://www.nngroup.com/articles/formative-vs-summative-evaluations/>):
*"Formative evaluations focus on determining which aspects of the design work well or not, and why.
These evaluations occur throughout a redesign and provide information to incrementally improve the
interface."* vs *"Summative evaluations describe how well a design performs, often compared to a
benchmark such as a prior version of the design or a competitor… assessing the overall experience of
a finished product."* And: *"The ultimate summative evaluation is the go/no-go decision of whether to
release a product."*

The distinction matters to a board because the **sample size and the recorded artefact differ**. A
formative session produces a list of problems from a handful of people; Nielsen's "Why You Only Need
to Test with 5 Users" (<https://www.nngroup.com/articles/why-you-only-need-to-test-with-5-users/>)
argues *"Elaborate usability tests are a waste of resources. The best results come from testing no
more than 5 users and running as many small tests as you can afford"*, on the Nielsen–Landauer
problems-found curve. A summative claim ("this is faster/easier than before") needs a metric and a
baseline: task success rate, time-on-task, and SUS. SUS is John Brooke's 1986 ten-item, five-point
scale, "originally created as a 'quick and dirty' scale for administering after usability tests"
and now "an industry standard with references in over 600 publications" (Sauro,
<https://measuringu.com/sus/>). Heuristic evaluation and cognitive walkthrough are the two expert
inspection methods that need no users at all
(<https://www.nngroup.com/articles/how-to-conduct-a-heuristic-evaluation/>,
<https://www.nngroup.com/articles/cognitive-walkthroughs/>) — **these are the HCI methods most
amenable to an LLM playing the first pass**, because they are a checklist applied by an expert to
an artefact rather than a measurement of a person.

**Automated playtesting.** The games literature has been doing "agent plays, human judges" for a
decade and has clear findings on where the line is.

- Zhao et al. (Electronic Arts), "Winning Isn't Everything: Enhancing Game Development with
  Intelligent Agents", *IEEE Trans. Games* (<https://arxiv.org/abs/1903.10545>): *"Unlike the agents
  built to 'beat the game', our agents aim to produce **human-like behavior to help with game
  evaluation and balancing**. We discuss two fundamental metrics based on which we measure the
  human-likeness of agents, namely **skill and style**."* With the cost warning: *"the learning
  potential of state-of-the-art deep RL models does not seamlessly transfer from the benchmark
  environments to target ones without heavily tuning their hyperparameters, leading to linear
  scaling of the engineering efforts and computational cost with the number of target domains."*
- Gordillo et al. (EA/SEED), "Improving Playtesting Coverage via Curiosity Driven Reinforcement
  Learning Agents" (<https://arxiv.org/abs/2103.13798>): agents rewarded for novelty "maximize game
  state coverage… thus providing the necessary data to identify potential issues", with the paper's
  own framing being *coverage and data for design decisions*, not verdicts.
- Also in this vein: procedural personas for automated playtesting
  (<https://arxiv.org/abs/1907.06570>, <https://arxiv.org/abs/2107.11965>).

The consistent shape: **agents produce coverage, reachability, telemetry and human-likeness proxies;
humans decide whether it is fun.** No paper I found claims otherwise.

**Expert review / peer review as a QA mechanism.** The economics analogue is the cleanest
formalisation of the split this report is proposing, because a journal has literally institutionalised
it. AEA Data and Code Availability Policy (<https://www.aeaweb.org/journals/data/data-code-policy>):
*"Prior to acceptance, authors of papers that contain empirical work, simulations, or experimental
work must provide the data, code, and other details of the computations sufficient to permit
replication."* and *"**The AEA data editor will assess compliance with this policy, including by
conducting reproducibility checks** and verifying the accuracy of the information provided."* DCAS #8
requires that analysis programs *"reproduce all the computational exhibits in the paper and approved
online appendices with minimal human intervention."* So: a **mechanical lane** (a non-author re-runs
the package and diffs the exhibits) and a **human expert lane** (referees judge whether the
specification answers the question) run in parallel, with different judges and different artefacts.
I could not fetch Bacchelli & Bird's "Expectations, Outcomes, and Challenges of Modern Code Review"
(ICSE 2013) — all mirrors I tried returned 403/404 — so the code-review evidence base is
**unverified** here.

**LLM agents as testers.** Two strands, with different evidence quality.

*GUI testing — real, measured gains, but on checkable faults.*

- GPTDroid (Liu et al., ICSE 2024, <https://arxiv.org/abs/2310.15780>): formulates mobile GUI testing
  as Q&A with the LLM; evaluated on 93 Google Play apps, *"outperforms the best baseline by 32% in
  activity coverage, and detects 31% more bugs at a faster rate… identify 53 new bugs on Google Play,
  of which 35 have been confirmed and fixed."*
- Trident (Liu et al., <https://arxiv.org/abs/2407.03037>) targets exactly the oracle gap: *"Existing
  automated GUI testing methods, **constrained by the lack of reliable testing oracles**, are limited
  to detecting crash bugs with obvious abnormal signals. Consequently, many non-crash functional
  bugs… often evade detection."* Three agents (Explorer, Monitor, Detector); on 590 non-crash bugs,
  *"14%-112% and 108%-147% boost in average recall and precision compared with the best baseline."*

  Read in Barr et al.'s terms: **LLM GUI agents are pushing the implicit-oracle frontier outward** —
  from "it crashed" to "the screen after this transition doesn't show what this flow implies". That
  is a real and useful expansion of the checkable set. It is not taste.

*Simulated users — useful for rehearsing the study, not for replacing the participant.*

- UXAgent (Lu et al., CHI EA 2025, <https://arxiv.org/abs/2502.12561>; system paper
  <https://arxiv.org/abs/2504.09407>) frames it exactly the way the owner's workflow does: *"to
  support UX researchers in **evaluating and reiterating their usability testing study design
  before they conduct the real human subject study** … automatically generate thousands of simulated
  users to test the target website. The results are shown in qualitative (e.g., interviewing how an
  agent thinks), quantitative (e.g., # of actions), and video recording formats."* And the honest
  finding: in a heuristic evaluation with five UX researchers, *"participants praised the innovation
  of our system but also expressed concerns about the future of LLM Agent-assisted UX study."*
- Agnew et al., "The illusion of artificial inclusion", CHI 2024
  (<https://arxiv.org/abs/2401.08572>): a scoping review of substitution proposals finds they are
  *"motivated by goals such as reducing the costs of research and development work and increasing
  the diversity of collected data. However, these proposals ignore and ultimately conflict with
  foundational values of work with human participants: representation, inclusion, and
  understanding."*
- Tjuatja et al., "Do LLMs exhibit human-like response biases? A case study in survey design", TACL
  (<https://arxiv.org/abs/2311.04076>): across nine models, *"popular open and commercial LLMs
  generally fail to reflect human-like behavior, particularly in models that have undergone RLHF.
  Furthermore, even if a model shows a significant change in the same direction as humans… they are
  sensitive to perturbations that do not elicit significant changes in humans."*
- The strongest positive result I found is also the one that shows the price of admission: Park et
  al., "LLM Agents Grounded in Self-Reports Enable General-Purpose Simulation of Individuals"
  (<https://arxiv.org/abs/2411.10109>) built agents from **two-hour interviews with 1,052 Americans**
  and reached *"83% … of participants' own two-week test-retest consistency benchmark"* on held-out
  GSS items, vs 74% for demographics-only agents. Good simulation of a person required a two-hour
  interview *with that person*. There is no shortcut to a synthetic user who stands in for the owner.

*LLM/agent as judge.*

- Zheng et al., "Judging LLM-as-a-Judge with MT-Bench and Chatbot Arena", NeurIPS 2023 D&B
  (<https://arxiv.org/abs/2306.05685>): *"strong LLM judges like GPT-4 can match both controlled and
  crowdsourced human preferences well, achieving **over 80% agreement, the same level of agreement
  between humans**"* — but the paper leads with the failure modes: *"position, verbosity, and
  self-enhancement biases, as well as limited reasoning ability."*
- Chiang & Lee, "Can Large Language Models Be an Alternative to Human Evaluations?", ACL 2023
  (<https://arxiv.org/abs/2305.01937>): LLM evaluation "is consistent with the results obtained by
  expert human evaluation" on story generation and adversarial attacks, and is *stable* across
  instruction formats and sampling — the reproducibility argument for the checklist lane.
- Zhuge et al., "Agent-as-a-Judge: Evaluate Agents with Agents"
  (<https://arxiv.org/abs/2410.10934>): *"Contemporary evaluation techniques are inadequate for
  agentic systems. These approaches either focus exclusively on final outcomes — ignoring the
  step-by-step nature of agentic systems, or require excessive manual labour."* On DevAI (55 tasks,
  365 hierarchical user requirements) it *"dramatically outperforms LLM-as-a-Judge and is as reliable
  as our human evaluation baseline"*. Note the shape of the win: it comes from **judging against an
  explicit hierarchy of stated requirements**, not from judging taste. An agent judge is strong
  exactly where a checklist exists.

**Summary of what AI can and cannot stand in for, on this evidence:**

| Can | Cannot |
|---|---|
| Drive the app and reach the state (GPTDroid: +32% activity coverage) | Be the person whose preference is the question (Tjuatja et al.; Agnew et al.) |
| Detect non-crash functional bugs with visual/logical cues (Trident) | Supply the "representation, inclusion and understanding" that having a participant is for |
| Judge stated requirements, hierarchically (Agent-as-a-Judge, as reliable as a human baseline) | Judge unstated quality — "quality is an opinion, not a fact" |
| Reproduce preference judgements ~80% of the time, stably and cheaply (MT-Bench; Chiang & Lee) | Be trusted on the remaining 20% where the bias modes (position, verbosity, self-enhancement) live |
| Rehearse the scenario so the human's session isn't wasted (UXAgent's explicit purpose) | Replace the session |

---

## 6. Existing schemes that classify *work* by required assurance

These are the precedents for "this card needs THIS much and THIS kind of QA". Every one of them
derives the level from **consequence of failure**, and then maps the level to a **required set of
activities** — not to "more testing" in the abstract.

**DO-178C — Design/Development Assurance Level** (<https://en.wikipedia.org/wiki/DO-178C>). The level
*"is determined from the safety assessment process and hazard analysis by examining the effects of a
failure condition"*: Catastrophic / Hazardous / Major / Minor / No effect. Then:

| Level | Failure condition | Objectives | …**with independence** |
|---|---|---|---|
| A | Catastrophic | 71 | 30 |
| B | Hazardous | 69 | 18 |
| C | Major | 62 | 5 |
| D | Minor | 26 | 2 |
| E | No safety effect | 0 | 0 |

*"The software level establishes the rigor necessary to demonstrate compliance."* And the definition
of independence: *"a separation of responsibilities where the objectivity of the verification and
validation processes is ensured by virtue of their 'independence' from the software development
team. For objectives that must be satisfied with independence, the person verifying the item … may
not be the person who authored the item and this separation must be clearly documented."*

**Independence is the dial to steal.** It is not "how many tests", it is *who is allowed to judge*,
and it scales 30 → 18 → 5 → 2 → 0 with consequence. In a board where AI agents implement cards, the
three lanes needs-verification → needs-qa-llm → needs-qa-human are precisely an independence ladder:
the author's own checks, a different agent's checks, a person's judgement. DO-178C says which cards
deserve which rung, and says the rung must be *documented*, which a card already does.

**ISO 26262 ASIL** (<https://en.wikipedia.org/wiki/Automotive_Safety_Integrity_Level>). A, B, C, D
plus **QM** ("Hazards that are identified as QM do not dictate any safety requirements"). Determined
by *"looking at the **severity, exposure and controllability** of the vehicle operating scenario"*,
where controllability is "the relative likelihood that a typical driver can act to prevent the
injury". The three-factor decomposition, and especially **controllability**, is what a software
board is missing: a failure you can see and undo needs less pre-release assurance than one you
cannot. The existence of **QM** — a level that means "no special requirements, just normal quality
management" — is the formal precedent for a card whose answer is "no QA lane, the tests are it".

**IEC 62304 medical device software safety classes** (<https://en.wikipedia.org/wiki/IEC_62304>).
Classes A/B/C, with a table titled "Effect of safety classification on required development process
documentation": which documents and activities are required is a function of the class. Same idea as
DO-178C, expressed as required artefacts rather than objectives.

**IEC 61508 SIL** (<https://en.wikipedia.org/wiki/Safety_integrity_level>) — the parent scheme ASIL
adapts.

**NASA NPR 7150.2D, Appendix D — software classification**
(<https://nodis3.gsfc.nasa.gov/displayDir.cfm?Internal_ID=N_PR_7150_002D_&page_name=AppendixD>). This
is the most directly transferable of the six, because it classifies *software by its role*, not by a
hazard analysis, and it states its factors:

> *"These definitions are based on: (1) usage of the software with or within a NASA system, (2)
> criticality of the system to NASA's major programs and projects, (3) **extent to which humans
> depend upon the system**, (4) developmental and operational complexity, and (5) extent of the
> Agency's investment. … Using the Requirements Mapping Matrix, **the number of applicable
> requirements and their associated rigor are scaled back for lower software classes.** Situations in
> which a project contains separate systems and subsystems having different software classes are not
> uncommon."*

Classes in 7150.2D: **A** human-rated space flight; **B** non-human-rated space or large-scale
aeronautics; **C** mission support / aeronautic vehicles / major facility; **D** basic
science/engineering design, research and technology; **E** design concept, research and
general-purpose — *"Software developed to explore a design concept or hypothesis but not used to
make decisions for an operational Class A, B, or C system… A defect in Class E software may affect
the productivity of a single user or small group of users but generally will not affect mission
objectives or system safety"*, with examples including *"parametric models to estimate performance
or other attributes of design concepts; software to explore correlations between data sets; line of
code counters; file format converters"*; **F** general-purpose computing, business and IT.
(A–F in 7150.2D; I did **not** verify whether earlier revisions ran to Class H, so treat "Class A–H"
as unverified.)

Three rules inside it that a board should copy verbatim:

1. *"For a given system or subsystem, software is expected to be uniquely defined within a single
   class. If more than one software class appears to apply, then **assign the higher of the
   classes**."* — the tie-break rule for a card with two consumers.
2. **Sub-systems within one project carry different classes.** NASA says mixed classes in one project
   are "not uncommon". This is exactly the owner's point, stated by a federal agency in 2022.
3. **Escalation on promotion:** Class E explicitly says *"Once the design concept is demonstrated,
   and a program agrees to incorporate it for flight or ground operational use … then the software
   should be upgraded to its appropriate classification."* A prototype card and a ship-it card for
   the same code are different classes. A board should re-classify on promotion, not inherit.

**Change-risk classification and testing in production.** Here the evidence is thinner and I want to
be honest about it. I **could not verify** a published, formal Google or Microsoft "change risk
class" taxonomy; the sources I could verify are adjacent:

- Google SRE workbook, canarying (above): a risk-tiered rollout is the *mechanism*, with the
  empirical premise that most incidents come from pushes, and feature flags used to *"separate
  feature launches from binary releases"* so components with different rates of change can be
  shipped and reverted independently.
- Google SRE book, SLOs (above): error budget as the gate on rollout — a *quantitative* change-risk
  policy.
- Facebook/Meta predictive test selection (<https://arxiv.org/abs/1810.05286>) and Google's "Taming
  Google-Scale Continuous Testing" (<https://research.google/pubs/taming-google-scale-continuous-testing/>)
  are the industrial precedent for *machine-learned* per-change risk driving which tests run —
  I fetched the landing pages but did not read the full texts, so treat their details as unverified.

The general lesson that *is* well supported: at scale the industry does not classify changes to
decide *whether* to test, it classifies them to decide *what runs before the push* and *how much of
production sees it first*. Reversibility is bought, not assumed.

---

## 7. PROPOSAL — a classification a Kanban tool can apply per card

### 7.1 The idea in one paragraph

Every scheme in §6 derives required assurance from *consequence*, and every scheme in §1–§3 derives
the *kind* of judgement from *whether an oracle exists* and *whose viewpoint the quality is defined
from* (GQM's `viewpoint`; ISO 25010's "specified users … contexts of use"; Marick's business-facing
axis). So: **three questions, asked at planning time, answered on the card; a fourth derived from
history.** Their combination selects a *QA shape* — a named recipe saying what is staged, who plays
it first, who judges, what is recorded — and a *rung on the independence ladder* saying which lane
the card must pass through. The classification is a **default with a reason-field override**, per
the context-driven principle that a scheme applied first and context second is "context-aware, but
not context-driven".

### 7.2 The questions

**Q-A. Who or what consumes the result?** (GQM `viewpoint`; Marick's business/technology axis;
ISO 25010's context-of-use anchor; ISTQB Q1–Q4.) One of:

| Value | Meaning | Grounding |
|---|---|---|
| `experience` | the output *is* a felt experience — feel, legibility, delight, pace | ISO 25010 Interaction Capability ("user engagement… inviting and motivating"); Marick Q3; Bach & Bolton "quality is an opinion, not a fact" |
| `decision` | a person reads the output and acts on it — a figure, a table, a report, a status pane | GQM subjective metrics; AEA's split of reproducibility check from referee judgement |
| `program` | another program consumes it across an interface | Marick technology-facing; specified oracles are usual here |
| `machine` | no reader — a daemon, a limiter, a build step, a pipeline | ISTQB Q4: "smoke tests and non-functional tests (except usability tests)… often automated" |

*Tie-break:* if two apply, take the more demanding (`experience` > `decision` > `program` >
`machine`), copying NPR 7150.2D D.2 ("assign the higher of the classes").

**Q-B. Is there an automated oracle, and of what kind?** (Barr et al.'s four categories, verbatim.)

| Value | Meaning |
|---|---|
| `specified` | a spec, contract, invariant or assertion set decides pass/fail |
| `derived` | a reference implementation or prior version (pseudo-oracle / N-version / golden output), a **metamorphic relation set**, or a **known-truth simulation** decides it |
| `implicit` | only "obvious faults" — crash, hang, leak, race, assertion — are detectable |
| `none` | the judgement is a value judgement; the human *is* the oracle |

A card may carry `derived` *and* `none` (the estimator in §8.4 does): "the numbers can be checked
against a simulation, but whether that's the right simulation cannot be".

**Q-C. What does failure cost, and can it be undone?** (ISTQB risk level = likelihood × impact;
ASIL = severity × exposure × **controllability**; DO-178C failure conditions; SRE error budget and
canarying as purchased reversibility.)

| Value | Meaning |
|---|---|
| `qm` | annoyance only, reverted by the next commit — ISO 26262's "QM: no safety requirements", NASA Class E ("affect the productivity of a single user… will not affect mission objectives") |
| `reversible` | real users or real callers are affected, and a flag/rollback/undo puts it back within one release cycle |
| `public` | it leaves the building and cannot be quietly retracted — a published figure, a shipped binary, an API other people now depend on |
| `irreversible` | data loss, money, credentials, safety, a claim in print |

**Q-D (derived, not asked). Has a person already judged this surface against this scenario?** Read
from the card's history: is there a prior `## Verdict` on the same staged scenario, and did the
artefacts (screenshots, transcript, numbers) move outside the region that verdict was about?
Grounding: Bach & Bolton — *"for checking to be considered good, it must happen in the context of a
competent testing process. Checking is a tactic of testing"* — plus ISTQB principle #5, "tests wear
out". If `yes` and the change is inside the judged envelope, the human lane **demotes to a checklist
re-run**; if `no`, or the artefacts moved, it escalates.

### 7.3 The independence ladder

Directly from DO-178C's "with independence" column and NASA's Requirements Mapping Matrix, and it
maps onto lanes the board already has:

| Rung | Who may judge | Board lane | Triggered by |
|---|---|---|---|
| 0 | the implementing agent, its own tests | `done` on a green Check | `qm` **and** oracle `specified`/`derived` |
| 1 | a *different* agent re-runs a written checklist | `needs-qa-llm` | anything above `qm`, or oracle `implicit` |
| 2 | a person plays the staged scenario and judges | `needs-qa-human` | consumer `experience`; or oracle `none`; or cost `public`/`irreversible` |
| 3 | a person **who is not the requester** judges, and the verdict is archived as a baseline | `needs-qa-human` + a named second reader | cost `irreversible` **and** oracle `none` |

The rule that makes this cheap: **rung 2 is never entered cold.** An agent always plays the scenario
first and publishes the artefacts (UXAgent's stated purpose: rehearse the study design before
spending a human), and it answers every question that is a *proposition* (Bach & Bolton) before the
person is asked anything.

### 7.4 The QA shapes

Eight shapes cover the combinations that matter. Each says: **staged**, **first player**, **judge**,
**recorded**, **gate**.

**S1 · CHECK.** `program`/`machine` consumer, `specified` oracle, `qm`/`reversible` cost.
*Staged:* nothing beyond the test fixture. *First player:* the test runner. *Judge:* the assertion.
*Recorded:* the invocation, in `## Tests`. *Gate:* Check green ⇒ `done`.
Grounding: Marick Q1 / ISTQB Q1 ("should be automated and included in the CI process"); Bach &
Bolton "checking… can in principle be performed by a tool".
**No `## QA checklist`, no `## Human QA`.** A board that always demands both is spending attention it
did not need to spend.

**S2 · CHECK + DIFFERENTIAL.** As S1, but oracle `derived`. *Staged:* the reference — a prior
version, an independent implementation, a golden corpus, a metamorphic relation set. *First player:*
the runner. *Judge:* the comparison, to a stated tolerance. *Recorded:* **the relations and the
reference, and why those** — because per Barr et al. metamorphic relations "are usually manually
inferred from a white-box inspection", the relation set is a design artefact that outlives the run.
*Gate:* diffs inside tolerance.
Grounding: Barr §5.1 pseudo-oracles / N-version; Segura et al. on relation design.

**S3 · ORACLE-DESIGN REVIEW.** Oracle is `derived` *and* `none`: the numbers are checkable against a
construct, but the construct itself is a judgement. *Staged:* a known-truth simulation or relation
suite, run and tabulated. *First player:* an agent writes and runs it and reports the table. *Judge:*
**two** — an agent judges the numbers against the stated tolerances (rung 1); a domain expert judges
*whether those are the right relations and the right generating process* (rung 2). *Recorded:* the
DGPs, seeds, tolerances, the coverage/bias table, and the expert's sign-off on the relation set.
*Gate:* both.
Grounding: Barr §5.1's "non-testable programs"; Segura §5.1.5 and Xie et al.'s split — violations of
*necessary* properties are verification, violations of *expected* properties are validation.

**S4 · FITNESS FUNCTION / SLO.** `machine` consumer, non-functional quality. *Staged:* a load,
soak or architecture-conformance harness. *First player:* the pipeline. *Judge:* a threshold on a
measured quantity, plus an error budget. *Recorded:* the number, the window, the rollback trigger.
*Gate:* threshold met, **and** — where reversibility is bought rather than assumed — a canary window
in production with an automatic revert.
Grounding: Ford/Parsons/Kua fitness functions ("an objective integrity assessment of some
architectural characteristics"); SRE SLOs ("100% is… unrealistic and undesirable"; error budget "used
as an input to the process that decides when to roll out"); SRE canarying ("a partial and
time-limited deployment… and its evaluation").
This is **the lane for engineering subsystems**, and it contains no human.

**S5 · SCENARIO, AI PLAYS, AI JUDGES.** Human-facing surface, but every question on the card is a
proposition about the artefact. *Staged:* a named situation with fixed seed data, including the
boundary states (empty, one, many, error). *First player:* an agent drives the real UI. *Judge:* the
same agent, against a numbered checklist. *Recorded:* screenshots/recordings at every step, plus
pass/fail per item with the evidence path. *Gate:* all items pass.
Grounding: GPTDroid (+32% activity coverage, 53 new Play Store bugs); Trident ("infer test oracles
for non-crash bugs", +108–147% precision) — LLM agents genuinely extend the checkable set; and
Agent-as-a-Judge, which is "as reliable as our human evaluation baseline" *when judging against an
explicit requirement hierarchy*.

**S6 · SCENARIO, AI PLAYS, HUMAN JUDGES.** Consumer `experience`, or oracle `none`. *Staged:* the
smallest realistic situation that puts the question in front of the person — realistic, recognisable
input, per Barr §7.2 ("arbitrary inputs are much harder to understand than recognisable pieces of
data, thus adding time to the checking process"). *First player:* an agent, which (a) proves the
scenario is reachable, (b) captures the artefacts, (c) answers every checkable item so the person is
asked **only** the value questions. *Judge:* the person. *Recorded:* the artefacts, the exact
questions, the verdict, and one free-text line on *what changed in the feel* — this line is the
baseline Q-D compares against next time. *Gate:* the person's verdict.
Grounding: Marick on why product critique needs the working artefact ("Manipulation is different
than cogitation"); Bach & Bolton ("quality is an opinion, not a fact"); UXAgent (simulate to rehearse
the study, not to replace the participant); Agnew et al.; Tjuatja et al.
**Ask one kind of question per session.** Marick's own finding: mixing bug-finding with
idea-generating means only the bugs get found.

**S7 · REPRODUCTION + EXPERT READ.** Consumer `decision`, cost `public`/`irreversible`. Two
parallel lanes on the same artefact, copying the AEA: (a) *mechanical* — a clean-room re-run from
raw inputs, with every number in the prose diffed against the regenerated exhibit; agent judges; (b)
*expert* — a domain reader judges whether the artefact answers the question it claims to. *Recorded:*
the environment digest, the diff, the expert's note. *Gate:* both.
Grounding: AEA Data and Code Availability Policy; DCAS #8 ("reproduce all the computational exhibits…
with minimal human intervention"); IEEE 1012's two questions.

**S8 · SUMMATIVE BENCHMARK.** Only when the card's claim is *comparative* ("faster", "clearer",
"easier"). *Staged:* the same task on old and new, with a pre-declared metric. *First player:* an
agent runs the mechanical half (time-on-task where it is machine-measurable, step counts, error
counts). *Judge:* people, enough of them; metric against baseline. *Recorded:* the metric, the n, the
baseline, the delta. *Gate:* the delta.
Grounding: NN/g formative vs summative ("often compared to a benchmark such as a prior version");
SUS; Nielsen's 5-user rule is for *formative* work and does **not** license a 5-person summative
claim — the tool should say which one the card is asking for, because a card that says "better"
needs a baseline and a card that says "fix this" does not.

### 7.5 The decision table

```
Q-A consumer   Q-B oracle          Q-C cost            ->  shapes        rung
-------------  ------------------  ------------------  --  ------------  ----
machine        specified|derived   qm|reversible           S1 (+S2)       0
machine        specified|derived   public|irreversible     S1+S2+S4       1
machine|prog   implicit            any                     S4 + S5-style  1    <- "no oracle" is not "no QA"
program        specified           qm|reversible           S1             0
program        derived             any                     S2             0-1
program|dec    derived + none      any                     S3             2    <- estimator-shaped work
decision       specified(artefact) public|irreversible     S7             2-3  <- paper / report / figure
experience     specified|derived   qm|reversible           S5             1    <- checkable UI facts only
experience     none                any                     S5 then S6     2
experience     none                public|irreversible     S5 then S6+S8  3
any            any                 any, card claims
                                   a comparison            + S8           2
```

And Q-D as a modifier applied last: if a prior human verdict exists on the same staged scenario and
the artefacts have not moved outside its region, **S6 demotes to S5** with the prior verdict quoted
as the standard. If they have moved, or no verdict exists, S6 stands.

### 7.6 Card fields

Four scalars and one derived block, which is what a `qa:` block on the card should carry:

```yaml
qa:
  consumer:   experience | decision | program | machine   # Q-A  (GQM viewpoint)
  oracle:     [specified | derived | implicit | none]     # Q-B  (Barr et al.) - a list
  cost:       qm | reversible | public | irreversible     # Q-C  (ASIL/DO-178C/ISTQB)
  comparative: true|false                                 # does the card claim "better than"?
  # derived, not typed:
  shape:      [S2, S6]        # from the table
  rung:       2               # independence: 0 self, 1 other agent, 2 person, 3 second reader
  override:   "why this card departs from the table"      # context-driven escape hatch
```

`shape` and `rung` are derived, and `override` is the only place a human types a departure — with a
reason, because context-driven testing's objection to schemes is not that they exist, it is that
they get applied before the context is looked at.

---

## 8. Worked classification of the six cards

### 8.1 Platformer jump physics tweak

`consumer: experience` · `oracle: [derived, none]` · `cost: reversible` · `comparative: true` (a
"tweak" implies better than before) → **shapes S2 + S6 (+S8 if the claim is comparative), rung 2.**

- **Automatable part (S2):** recorded input tapes replayed against the physics step are a genuine
  derived oracle — apex height, time-to-apex, coyote-time window, terminal velocity, and *"the
  canonical jump-puzzle set is still completable"* as a golden trace. Curiosity-driven or persona
  agents supply coverage that a human playtester cannot (Gordillo et al.: agents "maximize game state
  coverage… providing the necessary data to identify potential issues").
- **Non-automatable part (S6):** "does it feel floaty" has no oracle. EA's own framing is that agents
  are built for *human-likeness* precisely to help "game evaluation and balancing" — they measure
  skill and style, and the paper reports the engineering cost scales linearly per new domain. Agents
  cover; people judge feel.
- **Staged:** one save file, three named jumps (a just-reachable gap, a wall-kick, a drop-through),
  old build and new build side by side.
- **First player:** an agent proves all three are reachable in both builds and captures recordings +
  the trajectory plots.
- **Judge:** the owner, asked exactly one kind of question (Marick's warning).
- **Recorded:** tapes, trajectory numbers, both recordings, the verdict, and one line on what changed
  in feel — which becomes next time's Q-D baseline.
- **Gate:** tapes inside tolerance **and** the owner's verdict.

### 8.2 CLI punctuation fixer

`consumer: decision` (a person reads the output, but each transformation is a fact not a taste) ·
`oracle: [specified, derived]` · `cost:` **depends on whether it rewrites in place** — `qm` with
`--dry-run`/backup, `irreversible` without → **S1 + S2, rung 0–1. No human QA lane.**

- The rules are a specification; each is a proposition; a machine settles it. Bach & Bolton: this is
  checking, end to end.
- Strong derived oracles are free here: **idempotence** `f(f(x)) == f(x)`, **non-interference** (only
  punctuation bytes differ — diff the two strings with punctuation masked), **content invariance**
  under the identity-preserving rules, and a **golden corpus** of real documents. These are textbook
  metamorphic relations (Segura et al.'s point is that relations need not be numeric — Zhou et al.
  built them for search engines).
- **This is the card that proves the classifier earns its keep.** It is "human-facing" in the trivial
  sense and needs *zero* human QA. The only human-shaped question — *are these the right rules?* — is
  an **acceptance-criteria** question and belongs in planning as agreed examples (Adzic), not in a QA
  lane. A board that routes it to `needs-qa-human` is spending the owner on something a `## Tests`
  block already settled.
- The one thing that *does* escalate it: if it rewrites files in place with no undo, `cost:
  irreversible` promotes rung and the required addition is not a human reviewer but a **safety
  mechanism plus its test** (dry-run default, backup, atomic replace) — an ISO 25010 *Safety* /
  *fail-safe* item, checkable.

### 8.3 Rate limiter in a backend

`consumer: machine` · `oracle: [specified, derived, implicit]` · `cost: reversible` (flag + rollback)
→ **S1 + S2 + S4, rung 1. No human QA lane.**

- **Specified:** the algorithm has invariants — never more than N admitted per window; monotonic
  under increasing arrival rate; a client under the limit is never rejected; the counter cannot go
  negative. Property tests over a simulated clock settle all of these.
- **Derived:** a reference implementation or a discrete-event simulation at known arrival rates
  (pseudo-oracle, Barr §5.1).
- **Implicit:** no deadlock, no unbounded memory under sustained load, no race under concurrency
  (Barr §6 — and note these are exactly the faults the implicit oracle category was invented for).
- **S4 is the real gate:** acceptance is a fitness function plus an SLO — "p99 added latency < X ms at
  Y rps", "429 rate for compliant clients < Z" — and the last lane is a **canary**, not a person:
  "a partial and time-limited deployment of a change in a service and its evaluation", with the error
  budget deciding whether the rollout proceeds. This is what "verification continues into production"
  means and it is a lane the board currently has no name for.
- The human-shaped questions here are all **policy**: what limit, for whom, what does a limited
  client see. Acceptance criteria, agreed at planning. Not QA.
- If the limiter fronts something where being wrong costs money or locks out a paying customer,
  `cost` rises to `public` and the addition is not a person playing a scenario but a **staged
  rehearsal of the rollback** (an operational-acceptance item, in ISTQB's sense).

### 8.4 Difference-in-differences estimator

`consumer: [program, decision]` → take `decision` · `oracle: [derived, none]` · `cost:
irreversible` (a silently wrong estimator poisons every paper downstream and never crashes) →
**S3, rung 2–3.**

This is Davis & Weyuker's **non-testable program**, in the survey's own words: *"Programs which were
written in order to determine the answer in the first place. There would be no need to write such
programs, if the correct answer were known."* Three derived oracles exist and all three should be on
the card:

1. **Known-truth simulation.** Generate from a DGP with a known treatment effect τ; check bias → 0
   and CI coverage → nominal as n grows; check the estimator recovers τ when parallel trends hold by
   construction, and *fails visibly* when they do not.
2. **Pseudo-oracle.** Agree to tolerance with an independent implementation (a published package) on
   a canonical dataset — Barr §5.1's N-version idea, k = 2.
3. **Metamorphic relations.** Invariance to unit relabelling and to permuting rows; equivariance
   under rescaling the outcome (β scales by c) and shifting it (β unchanged); exact equality with the
   2×2 OLS interaction in the two-period two-group case; placebo — permuting treatment assignment
   yields effects centred on zero; weight invariance under duplicating a unit.

**Two judges, and the split is the whole point.** An agent can judge (1)–(3) *against the stated
tolerances* — mechanical, reproducible, rung 1. A human econometrician must judge **whether those are
the right DGPs and the right relations**: staggered adoption, negative weights, clustering, the
estimand actually targeted. That second judgement cannot be delegated because the failure mode is
"you simulated the world you assumed", and no relation you wrote will catch a world you did not
consider. This is exactly Xie et al.'s necessary-vs-expected distinction: relation violations are
*verification*; whether the relation set covers the claim is *validation*.

*Recorded:* DGP definitions, seeds, tolerances, the bias/coverage table, the reference package and
version, and the expert's sign-off on the relation set — the last of which is the durable artefact,
because the relations are reusable and the run is not.

### 8.5 Regression table for a paper

`consumer: decision` · `oracle: [specified]` **at the artefact level**, `none` at the claim level ·
`cost: public`/`irreversible` (retraction is the undo, and it is expensive) → **S7, rung 2–3.**

The AEA has already designed this workflow and the board should copy it:

- **Mechanical lane (agent, rung 1):** re-run the master script from raw inputs in a clean container
  and diff every number; check every number quoted in the prose against the regenerated table; check
  N, the clustering level, the significance thresholds behind the stars, and that the sample
  restrictions described in the text are the ones the code applies. DCAS #8's standard: programs
  *"reproduce all the computational exhibits in the paper… with minimal human intervention"*, and the
  AEA Data Editor *"conduct[s] reproducibility checks"* as a distinct institutional role.
- **Expert lane (person, rung 2):** does the specification answer the question; is the sign
  plausible; is the identifying assumption stated and defended; is the table the right table. This is
  referee work and there is no automated oracle for it.
- *Recorded:* container digest, the byte-level diff, the expert's note.
- **The value of the split is that the human never spends attention on "did the numbers change".**
  That is the checklist lane's job, it is fully mechanical, and it is the part that actually goes
  wrong most often.

### 8.6 A Kanban board's new test-history pane

`consumer: [experience, decision]` → take `experience` · `oracle: [specified, implicit, none]` ·
`cost: qm`–`reversible` → **S5 then S6, rung 2 the first time, rung 1 thereafter (Q-D).**

- **Specified (S1/S5):** given a seeded history, the pane shows these rows in this order with these
  states. Fully checkable, and it is most of the card.
- **Implicit:** no crash on a 10 000-run history, no hang, no unbounded memory.
- **None:** *"looking at this for three seconds, do you know which test to worry about?"* No oracle.
  ISO 25010 calls this Interaction Capability — appropriateness recognizability and
  self-descriptiveness — and its definition is anchored to "specified users… in a variety of contexts
  of use", which is the standards-body way of saying there is no machine judge.
- **Staged:** a seeded history containing one flaky test, one newly-broken test, one long-green test,
  a 200-run history and an empty state; plus the 80-column and the 200-column window.
- **First player (S5, agent):** drives the pane through all six states, captures a screenshot of
  each, and judges the propositions — empty state present and non-blank; dates readable; no
  truncation at 80 columns; keyboard-reachable; no crash at 10k rows; the flaky test is visually
  distinguished from the broken one *by some means* (that it *is* distinguished is checkable; that
  the means *works* is not). LLM GUI agents are demonstrably good at this class — Trident's whole
  contribution is inferring oracles for non-crash functional bugs from screenshot sequences.
- **Judge (S6, person):** the three-second question, and only that, against those exact screenshots.
- **Recorded:** the six screenshots, the checklist result, the verdict, and one line naming *which
  visual element carried the answer*.
- **Q-D pays off here:** the next change to the pane re-runs S5 and compares screenshots; if the
  region the verdict was about has not moved, the card does **not** go to a person. That is how a
  human verdict turns into a check — Bach & Bolton's "checking is a tactic of testing", implemented.

---

## 9. What I could not verify

- **ISO/IEC 25010's quality-in-use model** — the 2011 characteristic list (effectiveness, efficiency,
  satisfaction, freedom from risk, context coverage) and the claim that the 2023 revision moved it to
  ISO/IEC 25019:2023. iso.org, ANSI and iTeh all refuse automated fetches. The 2023 *product* quality
  model is fully verified from iso25000.com.
- **NASA class range** — verified A–F in NPR 7150.2D (2022). The commonly cited "Class A–H" is from an
  earlier revision I did not read.
- **Heuristic Test Strategy Model contents** — verified that it exists at v6.3 (Dec 2024) with "four
  focus areas: test techniques, project elements, product factors, and quality criteria categories";
  the PDF itself would not download (satisfice.com's endpoint 500s), so I do not have its
  quality-criteria guidewords. Same for the SBTM paper.
- **Bacchelli & Bird, "Expectations, Outcomes, and Challenges of Modern Code Review"** (ICSE 2013) —
  every mirror returned 403/404. The code-review-as-QA evidence base in §5 is therefore thin.
- **A formal Google or Microsoft "change risk classification"** — I found the *mechanisms* (SLOs,
  error budgets, canarying, predictive test selection) but no published taxonomy of change risk
  classes. If one exists it is not where I looked.
- **ISO/IEC/IEEE 29119-2's process model in detail** — I verified the series structure and part
  titles from the standards' own site, not the process content (Part 1 is free from ISO but I could
  not fetch it).
- **Crispin & Gregory's quadrant *diagram* contents** — I verified the axes and Q3 from Marick's
  original posts and Crispin's 2011 article, and the full Q1–Q4 contents from the ISTQB CTFL v4.0.1
  syllabus, which cites "Marick 2003, Crispin 2008". I did not read the *Agile Testing* book.
- I did not read the full texts of the playtesting, predictive-test-selection or Google-scale-testing
  papers — abstracts and landing pages only, except where quoted.

---

## 10. Sources

**Oracles**
- Barr, Harman, McMinn, Shahbaz, Yoo, "The Oracle Problem in Software Testing: A Survey", IEEE TSE 41(5):507–525, 2015 — <https://doi.org/10.1109/TSE.2014.2372785>, full text <https://coinse.github.io/publications/pdfs/Barr2015qd.pdf>
- Segura, Fraser, Sánchez, Ruiz-Cortés, "A Survey on Metamorphic Testing", IEEE TSE 42(9):805–824, 2016 — <https://doi.org/10.1109/TSE.2016.2532875>, full text <https://eprints.whiterose.ac.uk/id/eprint/110335/>

**Standards**
- IEEE 1012 (2016/2024) — <https://standards.ieee.org/ieee/1012/5609/>
- ISO/IEC/IEEE 29119 series — <http://softwaretestingstandard.org/>
- ISO/IEC 25010 product quality model (current, 9 characteristics) — <https://iso25000.com/index.php/en/iso-25000-standards/iso-25010>
- ISO/IEC 9126 → 25010 history — <https://en.wikipedia.org/wiki/ISO/IEC_9126>

**Taxonomies**
- Marick, "My Agile testing project" (the matrix) — <http://www.exampler.com/old-blog/2003/08/21/>; "tests and examples" <http://www.exampler.com/old-blog/2003/08/22/>; "business-facing product critiques" <http://www.exampler.com/old-blog/2003/09/24.1.html>
- Crispin, "Using the Agile Testing Quadrants" (2011) — <https://lisacrispin.com/2011/11/08/using-the-agile-testing-quadrants/>
- ISTQB Certified Tester Foundation Level syllabus v4.0.1 (2024-09-15) — <https://www.istqb.org/wp-content/uploads/2024/11/ISTQB_CTFL_Syllabus_v4.0.1.pdf> (§5.1.6 pyramid, §5.1.7 quadrants, §5.2 risk-based testing, §1.3 principles, §2.2.1 acceptance)
- Fowler, "The Practical Test Pyramid" — <https://martinfowler.com/articles/practical-test-pyramid.html>; Dodds, "The Testing Trophy" — <https://kentcdodds.com/blog/the-testing-trophy-and-testing-classifications>
- Bach & Bolton, "Testing and Checking Refined" — <https://www.satisfice.com/blog/archives/856>; Bolton, "Testing vs. Checking" — <https://developsense.com/blog/2009/08/testing-vs-checking>
- Kaner, Bach, Pettichord, context-driven principles — <https://context-driven-testing.com/>
- Bach, Heuristic Test Strategy Model v6.3 — <https://www.satisfice.com/download/heuristic-test-strategy-model>; exploratory testing / SBTM — <https://www.satisfice.com/exploratory-testing>

**Acceptance**
- Adzic, *Specification by Example* — <https://gojko.net/books/specification-by-example/>
- Basili, Caldiera, Rombach, "The Goal Question Metric Approach" — <https://www.cs.umd.edu/~mvz/handouts/gqm.pdf>
- Thoughtworks Radar, "Architectural fitness function" — <https://www.thoughtworks.com/en-us/radar/techniques/architectural-fitness-function>; Paul & Wang, "Fitness function-driven development" — <https://www.thoughtworks.com/en-us/insights/articles/fitness-function-driven-development>
- Google SRE, "Service Level Objectives" — <https://sre.google/sre-book/service-level-objectives/>; SRE workbook, "Canarying Releases" — <https://sre.google/workbook/canarying-releases/>

**Human evaluation**
- NN/g, "Formative vs. Summative Evaluations" — <https://www.nngroup.com/articles/formative-vs-summative-evaluations/>; "Why You Only Need to Test with 5 Users" — <https://www.nngroup.com/articles/why-you-only-need-to-test-with-5-users/>; "How to Conduct a Heuristic Evaluation" — <https://www.nngroup.com/articles/how-to-conduct-a-heuristic-evaluation/>; "Cognitive Walkthroughs" — <https://www.nngroup.com/articles/cognitive-walkthroughs/>
- Sauro, "Measuring Usability with the System Usability Scale (SUS)" — <https://measuringu.com/sus/>
- AEA Data and Code Availability Policy — <https://www.aeaweb.org/journals/data/data-code-policy>

**AI as player / judge**
- Zhuge et al., "Agent-as-a-Judge" — <https://arxiv.org/abs/2410.10934>
- Zheng et al., "Judging LLM-as-a-Judge with MT-Bench and Chatbot Arena", NeurIPS 2023 — <https://arxiv.org/abs/2306.05685>
- Chiang & Lee, "Can Large Language Models Be an Alternative to Human Evaluations?", ACL 2023 — <https://arxiv.org/abs/2305.01937>
- Liu et al., GPTDroid, ICSE 2024 — <https://arxiv.org/abs/2310.15780>; Liu et al., Trident — <https://arxiv.org/abs/2407.03037>
- Lu et al., UXAgent — <https://arxiv.org/abs/2502.12561> and <https://arxiv.org/abs/2504.09407>
- Agnew et al., "The illusion of artificial inclusion", CHI 2024 — <https://arxiv.org/abs/2401.08572>
- Tjuatja et al., "Do LLMs exhibit human-like response biases?" — <https://arxiv.org/abs/2311.04076>
- Park et al., "LLM Agents Grounded in Self-Reports…" — <https://arxiv.org/abs/2411.10109>
- Zhao et al. (EA), "Winning Isn't Everything", IEEE ToG — <https://arxiv.org/abs/1903.10545>; Gordillo et al., "Improving Playtesting Coverage via Curiosity Driven RL Agents" — <https://arxiv.org/abs/2103.13798>; procedural personas — <https://arxiv.org/abs/1907.06570>, <https://arxiv.org/abs/2107.11965>

**Assurance levels**
- DO-178C / DAL — <https://en.wikipedia.org/wiki/DO-178C>
- ISO 26262 ASIL — <https://en.wikipedia.org/wiki/Automotive_Safety_Integrity_Level>
- IEC 61508 SIL — <https://en.wikipedia.org/wiki/Safety_integrity_level>
- IEC 62304 software safety classes — <https://en.wikipedia.org/wiki/IEC_62304>
- NASA NPR 7150.2D Appendix D, Software Classifications — <https://nodis3.gsfc.nasa.gov/displayDir.cfm?Internal_ID=N_PR_7150_002D_&page_name=AppendixD>
- Machalica et al., predictive test selection — <https://arxiv.org/abs/1810.05286>; Memon et al., "Taming Google-Scale Continuous Testing" — <https://research.google/pubs/taming-google-scale-continuous-testing/>
