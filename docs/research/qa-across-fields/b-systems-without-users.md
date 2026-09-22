# QA where there is no end user: engineering subsystems, infrastructure, embedded, safety-critical

Research slice B. Everything below is about work whose consumer is another program, a machine, or an
operator — not a person looking at a screen. The recurring shape, stated once up front:

> **Stage the adverse situation the work was written to survive; let a non-human oracle decide; record
> the decision as an artefact; have a *named, independent* person sign that the decision was reached
> honestly.**

The "human QA" step in these disciplines is almost never "a person uses the feature." It is a person
judging *whether the evidence is adequate* — a review, a witness signature, a readiness sign-off.
That distinction is the single most transferable finding for the Board design.

---

## 1. Verification vs validation, the V-model, traceability, the four methods

### 1.1 The definitions

NASA's Systems Engineering Handbook §5.3 states the distinction as two questions:

> "Was the end product realized right?" (verification) versus "Was the right end product realized?" (validation).
> — https://www.nasa.gov/reference/5-3-product-verification/

Verification is *against the specification*. Validation is *against the stakeholder's intent in the real
operating environment*. They are different activities with different oracles: verification's oracle is a
written requirement; validation's oracle is a stakeholder or an operational environment.

IEEE 1012 ("IEEE Standard for System, Software, and Hardware Verification and Validation") is the process
standard. Its central mechanism is the **integrity level**: four levels derived by crossing four
consequence levels (catastrophic, critical, marginal, negligible) with four likelihood levels
(reasonable, probable, occasional, infrequent). The *depth and intensity* of V&V activity is then a
function of integrity level — level 1 gets the lightest V&V, level 4 the heaviest.
- https://standards.ieee.org/ieee/1012/5609/
- Integrity-level mapping table reproduced at https://spectrum.ieee.org/regulating-ai-programs-roadmap/table-1-ieee-1012-standards-map-of-integrity-levels-onto-a-combination-of-consequence-and-likelihood-levels
- An older (1998) full text is public: https://profs.etsmtl.ca/claporte/english/enseignement/cmu_sqa/notes/verification/ieee%20_std_1012%20_sw%20_v%20&%20_v.pdf

**This is the most directly borrowable idea in the whole slice:** the *rigour is selected per item from a
risk classification*, not applied uniformly. Every standard here does this — IEEE 1012 integrity levels,
DO-178C DALs, ISO 26262 ASILs, IEC 62304 safety classes. A card-level "QA shape" picker is exactly an
integrity-level assignment.

### 1.2 The four verification methods — T / A / I / D

NASA §5.3 defines exactly four, and every requirement in a verification matrix is annotated with one
(or more) of them:

| Code | Method | NASA's definition | When used |
|---|---|---|---|
| **T** | Test | "The use of an end product to obtain detailed data needed to verify performance or provide sufficient information to verify performance through further analysis." | Quantitative evidence needed; most resource-intensive; the default for performance numbers |
| **A** | Analysis | "mathematical modeling and analytical techniques to predict the suitability of a design to stakeholder expectations based on calculated data or data derived from lower system structure end product verifications" | Physical article not available; conditions cannot be reproduced (orbital thermal, 10-year wear); or results must be extrapolated from component tests |
| **I** | Inspection | "The visual examination of a realized end product." | Physical design features, markings, presence/absence, conformance of a document or a drawing |
| **D** | Demonstration | showing "that the use of an end product achieves the individual specified requirement" | Qualitative "it does the thing" with no detailed data capture; mockups, simulators, operational exhibition by qualified personnel |

Source: https://www.nasa.gov/reference/5-3-product-verification/

The pairs are the useful part: **Test and Demonstration both run the thing; Test records numbers and
Demonstration records only that it happened.** Analysis and Inspection do not run it at all. A card's
"QA checklist" line is almost always a **T** or a **D**; a design/doc review line is an **I**; a
performance-budget argument is an **A**.

### 1.3 The V-model

Left arm descends through requirements → system design → architecture → module design; right arm ascends
through unit test → integration test → system test → acceptance test. The defining property is the
horizontal pairing: **each level on the left is verified by the level directly across from it on the
right, using the artefact written on the left as the oracle.** Unit tests answer to the module design;
acceptance tests answer to the original requirements.
- https://en.wikipedia.org/wiki/V-model_(software_development)

Criticisms are well known and worth knowing (rigid, testing compressed into an end-stage window when
earlier phases overrun, favours scripted tests over exploratory work), but it survives in ISO 26262 Part 6
and DO-178C because the pairing is what makes traceability mechanical.

### 1.4 Requirements traceability matrix (RTM)

NASA §5.3's outputs include "Verification reports documenting requirement traceability, methods used, and
results" and "**Compliance/verification matrices** tracing requirement satisfaction."

NASA's software requirement is bidirectional: the project manager must maintain traceability *both*
requirement→test and test→requirement, so that (a) no requirement is unverified and (b) no test exists
that verifies nothing. ISO 26262 Part 6 makes bidirectional traceability a standing obligation: "when code
changes occur during testing or customer requirements shift, the connections between requirements and
implementation must be continuously updated and verified" (https://ldra.com/iso-26262/).

A verification matrix row, in the form used across aerospace/defence:

```
Req ID | Requirement text | Method (T/A/I/D) | Verification level | Procedure ID | Result ref | Status
SRS-142 | The controller shall limit output current to 12.0 A ±0.2 A | T | Subsystem | ATP-07 §4.3 | TR-07-004 | PASS
SRS-143 | The controller shall be rated for 100,000 thermal cycles   | A | Component  | ANA-11      | ANA-11 R2  | PASS
```

Two properties worth stealing: **(i) the method is declared before the work starts**, and **(ii) the
result is a *reference to an artefact*, not a boolean.** "PASS" alone is never the record.

### 1.5 Verification planning and verification results — what NASA actually requires

- **SWE-028 (planning):** "The project manager shall plan software verification activities, methods,
  environments, and criteria for the project." Criteria must be predeclared: "Satisfaction criteria may be
  given in numerical form (a specific value, a minimum value, a range of values). They may also be in a
  pass/fail or true/false format." Notably on independence: "This requirement does not assign the
  responsibility for performing the software verification tasks to any specific organization."
  https://swehb.nasa.gov/display/SWEHBVB/SWE-028+-+Verification+Planning
- **SWE-030 (results):** "The project manager shall record, address, and **track to closure** the results
  of software verification activities." Recorded items include software identification and version, "an
  assessment of how the verification environment differs from the operational conditions," results per
  requirement, and each discrepancy individually with root cause and impact.
  https://swehb.nasa.gov/display/SWEHBVB/SWE-030+-+Verification+Results

The "assessment of how the verification environment differs from operational conditions" is a field worth
copying verbatim into any staged-scenario QA record. It is the field that stops a green staged run from
being read as a green production claim.

### 1.6 Acceptance test procedures, FAT and SAT

Acceptance testing is "Formal testing with respect to user needs, requirements, and business processes"
(ISTQB, via https://en.wikipedia.org/wiki/Acceptance_testing), judged against **acceptance criteria** —
"a set of conditions that is required to be met before deliverables are accepted" (PMI).

- **FAT (Factory Acceptance Test)** — run at the *supplier's* site, before shipment. It "usually includes
  a check of completeness, a verification against contractual requirements, a proof of functionality
  (either by simulation or a conventional function test), and a final inspection"
  (https://en.wikipedia.org/wiki/Factory_acceptance_testing). The customer sends a **witness**.
- **SAT (Site Acceptance Test)** — run by the *users at their own site* after installation, on the real
  plant with the real interfaces. Things that passed FAT against simulated I/O routinely fail SAT.
- **OAT (Operational Acceptance Testing)** — checks operational readiness: backups, disaster recovery
  procedures, maintenance protocols. This is the closest classical analogue to an SRE production
  readiness review.

The three-stage structure (FAT on simulated inputs at the builder's bench → SAT on the real rig →
operational acceptance) is the industrial answer to exactly the owner's problem: *how do you accept
something whose user is a machine?* You accept it three times, against a progressively realer environment,
and a different party witnesses each time.

**Caveat:** IEC 62381 is the standard that formalises FAT/SAT/SIT for process-industry automation systems.
I could not retrieve its scope text — the IEC webstore URL I tried returned a different standard. Treat
the IEC 62381 reference as unverified.

### 1.7 Test Readiness Review and the other gates

NASA NPR 7123.1D, Appendix G, gives entrance and success criteria tables for every lifecycle review. The
ones relevant here (table numbers confirmed from the document's contents page,
https://nodis3.gsfc.nasa.gov/displayDir.cfm?t=NPR&c=7123&s=1D):

| Review | Table | What it gates |
|---|---|---|
| Production Readiness Review (PRR) | G-8 | Readiness to begin production |
| **Test Readiness Review (TRR)** | G-10 | Readiness to *begin* a specific test — procedures approved, article configured and identified, environment ready, personnel trained, safety and abort criteria agreed |
| System Acceptance Review (SAR) | G-11 | Acceptance of the delivered system by the receiving party |
| Operational Readiness Review (ORR) | G-12 | Readiness to operate, with the operators and the procedures |

**TRR is the single most under-copied idea for a card workflow.** It is a formal gate whose subject is not
the product but *the test*: are the procedures written and approved, is the configuration under
identification, is the environment representative, do we agree in advance what constitutes a pass and what
constitutes an abort. A card that says "QA: verify the fix" without a TRR-equivalent has skipped the step
where the test becomes falsifiable.

**Caveat:** I confirmed the review names and Appendix G table numbers, but could not retrieve the full
entrance/success criteria text from the NODIS pages.

### 1.8 Independent V&V (IV&V)

IEEE 1012 Annex C defines IV&V. The classic three-axis definition of "independent" is **technical,
managerial and financial** independence — the IV&V team chooses its own techniques, does not report to the
project manager, and is not funded out of the project's budget (so the project cannot defund inconvenient
findings).

NASA runs this as a dedicated organisation: the Katherine Johnson IV&V Facility in Fairmont, WV, ~275
staff, which "assures the safety and success of software on NASA's highest-profile missions."
https://www.nasa.gov/centers-and-facilities/ivv/

**Caveat:** the NASA IV&V page did not itself spell out the technical/managerial/financial triad; I could
not verify that wording from a primary source in this session.

---

## 2. Safety-critical software

### 2.1 DO-178C (airborne software)

Design Assurance Level is assigned from the **severity of the failure condition** the software can
contribute to:

| DAL | Failure condition | Structural coverage required |
|---|---|---|
| A | Catastrophic | Statement + Decision + **MC/DC** (+ data & control coupling) |
| B | Hazardous / severe | Statement + Decision (+ data & control coupling) |
| C | Major | Statement |
| D | Minor | none |
| E | No safety effect | none |

Source: https://www.tasking.com/do-178/ (vendor summary of the paywalled RTCA standard)

**MC/DC** — Modified Condition/Decision Coverage — requires that each condition inside a decision be shown
to *independently* affect the decision's outcome. Its practical appeal is that it is near-exhaustive but
not combinatorial: for a six-condition decision it needs roughly **7 tests rather than 64** exhaustive
combinations (same source). AdaCore states the ladder plainly: "Statement coverage for Level C… Statement
and Decision coverage for level B… Statement, Decision and Modified Condition / Decision Coverage (MC/DC)
at level A."
https://learn.adacore.com/booklets/adacore-technologies-for-airborne-software/chapters/analysis.html

**Independence.** DO-178C marks certain objectives as requiring independence. AdaCore: "Independence is
required between code development and test case derivation, to satisfy the independence criteria of
objectives A6-3 and 4 for software level A and B." In practice this means *the engineer who wrote the code
may not be the engineer who derives the test cases or reviews the requirement* — at DAL A/B a separate
reviewer breaks the author-reviewer link. This is the clearest statement in any of these standards of the
rule a Board should encode: **the agent that implemented the card is disqualified from being the
verifier of the card.**

**Objective counts.** The widely cited figures are 71 objectives at Level A, 69 at B, 62 at C, 26 at D
(with A additionally requiring ~30 of those to be met with independence). I found this repeated in
secondary literature (e.g. https://arxiv.org/pdf/2511.14215) but RTCA DO-178C is paywalled and I could
**not verify these counts against a primary source.** Treat the numbers as indicative; the *shape*
(objective count and independence count both scale with criticality) is solid.

**Data items produced.** Plans: PSAC (Plan for Software Aspects of Certification), SDP, SVP, SCMP, SQAP,
TQP. Completion evidence: SCI (Software Configuration Index) and **SAS (Software Accomplishment Summary)**
— the document that says, objective by objective, how each was satisfied and where the evidence is. The
SAS is the artefact-shape worth stealing for a card: *a per-objective ledger with a pointer to evidence,
not a narrative*.

### 2.2 ISO 26262 (automotive)

ASIL (A–D, plus QM) is derived from three factors and a lookup table: **Severity (S0–S3) × Exposure
(E1–E4) × Controllability (C1–C4)**. https://ldra.com/iso-26262/

Part 6 covers software, structured as a V-model with mandatory bidirectional traceability. Its tables use a
three-value recommendation scale that is itself borrowable: **`++` highly recommended for this ASIL, `+`
recommended, `o` no recommendation for or against.** A card-level QA-method table with exactly these three
marks per rigor tier would be a faithful copy.

Testing ladder (LDRA):
- **MIL** — model in the loop
- **SIL** — software in the loop: model/code on a host computer
- **PIL / HIL** — processor / hardware in the loop: on the target
- **Back-to-back testing** — compare model outputs against generated-code behaviour (a differential test)
- **Fault injection** — "proves system robustness under failure conditions"

Fault injection is named in both Part 4 (hardware) and Part 6 (software)
(https://www.embitel.com/blog/embedded-blog/fault-injection-testing-of-safety-critical-automotive-software).

**Caveat:** LDRA's summary says "Higher ASILs (C, D) mandate MC/DC." The standard's Part 6 tables are
paywalled; the usual reading is that MC/DC is *highly recommended* at ASIL D and statement/branch coverage
at lower ASILs. I could not verify the exact per-ASIL table.

### 2.3 IEC 62304 / FDA (medical)

IEC 62304 safety classes, assigned from the harm a software failure could cause:
- **Class A** — "cannot contribute to a hazardous situation," or residual risk is acceptable
- **Class B** — can lead to a hazardous situation, but "the possible resulting harm is not severe"
- **Class C** — can lead to "serious harm or death"

The component with the highest class sets the system's class. Crucially, **external risk controls
(hardware interlocks, an independent monitor, a required user confirmation) can lower a component's
class** — i.e. you can buy down verification rigour with architecture.
https://blog.johner-institute.com/iec-62304-medical-software/safety-class-iec-62304/

Detailed design (clause 5.4) is required at Class C and optional at B; requirements, architecture, unit
verification and system testing are required at all three.

FDA's governing guidance is **"General Principles of Software Validation; Final Guidance for Industry and
FDA Staff," 11 January 2002** — https://www.fda.gov/regulatory-information/search-fda-guidance-documents/general-principles-software-validation
(PDF: https://www.fda.gov/media/73141/download). FDA differs from IEC 62304 in one instructive way: **FDA
lets documentation level be set per *function* (an aspect of intended use), whereas IEC 62304 requires a
safety class per software system or component.** Per-function rigour selection is much closer to
per-*card* rigour selection than per-module is.

**Caveat:** I read FDA's landing page metadata only, not the guidance body.

### 2.4 Hazard analysis as a QA *input*: FMEA and FTA

FMEA is bottom-up: for each item/function, enumerate failure modes → effects (local, next-higher, system)
→ causes → current controls → **Severity, Occurrence, Detection** ratings → RPN = S × O × D → recommended
actions. FMECA adds criticality. DFMEA covers design; PFMEA covers the manufacturing process. Standards:
SAE J1739, IEC 60812. https://en.wikipedia.org/wiki/Failure_mode_and_effects_analysis

The line that matters for QA design: **"FMEA results directly inform verification test plans, ensuring
critical failure modes receive appropriate validation coverage and detection capability assessment."** The
*Detection* column is literally a rating of how good your QA is at catching that failure mode — the hazard
analysis and the test plan are the same document read in two directions. FTA is the top-down dual: start
from the undesired event, decompose into contributing causes.

**This is the answer to "what scenario should we stage?"** In safety engineering you do not invent
scenarios; you read them off the hazard analysis. A card that fixes a bug *has* an implied failure mode —
the one that occurred. The staged scenario is that failure mode's cause chain, reproduced.

### 2.5 HIL, SIL and simulation — "the staged scenario for a controller"

HIL has three parts:
1. a **plant model** — the mathematical model of the thing being controlled (vehicle dynamics, hydraulics,
   an engine, a grid)
2. a **real-time simulator** running that model, electrically emulating the sensors and actuators
3. the **real embedded controller under test**, reading simulated sensor values and driving the model

https://en.wikipedia.org/wiki/Hardware-in-the-loop_simulation

Why it exists, in the article's own terms: "Testing at or beyond the range of certain ECU parameters" and
"Testing and verification of the system at failure conditions"; and economics — a jet engine costs
millions, a high-fidelity HIL rig "may cost merely a fraction of one engine."

**This is the exact structural analogue of the owner's "staged test case."** For a controller, the "staged
situation that simulates the problem" is *a plant model configured to reproduce the failure the code must
survive* — the sensor dropout, the stuck actuator, the brownout. Nobody is watching a screen. The judge is
an assertion on the plant model's state trajectory.

---

## 3. Infrastructure and backends

### 3.1 Contract tests (Pact) — the consumer is the oracle

"The contract is generated during the execution of the automated consumer tests." The consumer writes
example request/response pairs; the pact file is **"contract by example"** — concrete interactions, not an
abstract schema; the provider replays those examples against its own implementation. https://docs.pact.io/

The deployment gate is `can-i-deploy`, which asks: *is there a successful verification result between the
version about to be deployed and all versions of integrated applications already in the target
environment?* Its output is a matrix and an exit code:

```
$ pact-broker can-i-deploy --pacticipant Foo --version 24 --to-environment production
Computer says no ¯\_(ツ)_/¯
CONSUMER | C.VERSION | PROVIDER | P.VERSION | SUCCESS? | RESULT#
---------|-----------|----------|-----------|----------|--------
Foo      | 24        | Bar      | 56        | false    | 1
(exit code 1 means no)
```
https://docs.pact.io/pact_broker/can_i_deploy

The idea to borrow: **an integration's QA state is a matrix over (my version × their version × environment),
and "can I ship" is a query against it** — not a per-repo green tick.

### 3.2 API compatibility as a mechanical gate

`buf breaking` checks a protobuf schema against a baseline, with four rule categories ordered by strictness:
`FILE` (per-file generated-source breakage) ⊃ `PACKAGE` ⊃ `WIRE_JSON` ⊃ `WIRE` (binary wire format only).
Passing a stricter category implies passing the looser ones. Baselines can be a git branch, an image, or a
registry module, and it runs at three stages: locally, in CI as a PR check, and server-side at the registry
as a final gate requiring approval.

```
$ buf breaking --against '.git#branch=main'
```
https://buf.build/docs/breaking/

### 3.3 Load, stress, soak — and which number is the oracle

The canonical taxonomy (Grafana k6, https://grafana.com/docs/k6/latest/testing-guides/test-types/):

| Type | Purpose | Duration | What it catches |
|---|---|---|---|
| Smoke | "validate that your script works and that the system performs adequately under minimal load" | seconds–minutes | a broken test |
| Average-load | expected normal conditions | 5–60 min | regression against the SLO |
| Stress | "when load exceeds the expected average" | 5–60 min | degradation shape |
| **Soak / endurance** | "assess the reliability and performance of your system over extended periods" | hours | memory leaks, fd leaks, connection-pool exhaustion, slow data corruption |
| Spike | "sudden, short, and massive increases in activity" | minutes | recovery behaviour |
| Breakpoint | "gradually increase load to identify the capacity limits" | until failure | actual capacity |

The oracle is a **threshold declared before the run** — a p95 latency bound, an error rate, a saturation
ceiling — which is to say an SLO. Google's framing: canary metrics should start from "SLIs already tracked
for SLO compliance," then narrow to "the top few most indicative metrics (perhaps a dozen maximum)."
https://sre.google/workbook/canarying-releases/

Note the soak test's distinguishing property: **it can only fail with time**. There is a whole class of
defects for which no instantaneous check is an oracle, and the QA shape has to be "run it for six hours
and watch a derivative." Worth a dedicated card shape.

### 3.4 Canary and progressive delivery — automated analysis as the judge

Google's definition: a canary is "a partial, time-limited deployment exposing changes to a subset of
production traffic," compared against an unchanged **control** population. Blast-radius arithmetic: "A 5%
canary population receiving defective code producing 20% errors yields only 1% overall error impact."
Metric requirements: problem-indicating, attributable to the code (not infrastructure noise), and **never
before/after** — always canary-vs-control simultaneously, because before/after drags in traffic-pattern
shifts. https://sre.google/workbook/canarying-releases/

**Kayenta / NetflixACAJudge** (the default Spinnaker judge) is the concrete mechanism:
- each metric is classified **Pass / High / Low / Nodata / NodataFailMetric**
- significance is decided by a **Mann-Whitney U test** — "the judge needs to be 98% confident there's a
  real difference before flagging a metric," with a tolerance band of ±0.25 × the Hodges-Lehmann estimate
- effect-size thresholds (`allowedIncrease`, `allowedDecrease`) are a *second* gate, applied only after
  significance: "a metric with a 20% increase might still pass if the confidence interval is wide"
- **group score = (Pass count / Total count) × 100**; the **summary score is a weighted average of group
  scores**, with unweighted groups sharing the remaining weight equally
- the summary score is compared against a **pass threshold** and a **marginal threshold** → pass /
  marginal / fail

https://spinnaker.io/docs/guides/user/canary/judge/

**Argo Rollouts** does the same job declaratively, and adds the most interesting state:
- `AnalysisTemplate` declares metric providers (Prometheus, Datadog, a web endpoint), an interval, a
  `successCondition` (e.g. `result[0] >= 0.95`) and a `failureCondition` (e.g. `result[0] >= 10`)
- `failureLimit: 3` → "The entire analysis run is considered as Failed after three failed measurements";
  `consecutiveSuccessLimit` requires N consecutive passes
- **Inconclusive** — a measurement matching neither condition. The rollout *pauses* and waits for a human.
  The docs' own words: this enables "automation of analysis runs, and collect the measurement, but still
  allow human judgement."
- outcome → Successful: promote; Failed: abort immediately; Inconclusive: pause for a person

https://argo-rollouts.readthedocs.io/en/stable/features/analysis/

**Three-valued outcomes (pass / fail / inconclusive) are the most important single design borrowing in this
report.** It is the mechanism by which an automated judge hands off to a human *only* in the cases where
the automated judge genuinely has nothing to say — which is exactly the boundary the owner is trying to
draw between "QA checklist" and "Human QA."

### 3.5 Chaos engineering, game days, DiRT

Principles of Chaos Engineering (https://principlesofchaos.org/) — "the discipline of experimenting on a
system in order to build confidence in the system's capability to withstand turbulent conditions in
production." The method is an *experiment with a hypothesis*, not a test:

1. **Define steady state** as measurable output (throughput, error rate, latency percentiles)
2. **Hypothesise** "that this steady state will continue in both the control group and the experimental group"
3. **Introduce real-world variables** — "servers that crash, hard drives that malfunction, network
   connections that are severed"
4. **Try to disprove the hypothesis** by looking for a difference in steady state between control and
   experimental group

Advanced principles: vary real-world events prioritised "by potential impact or estimated frequency"; run
in production; automate and run continuously; **minimise blast radius**.

**Google DiRT** is the company-wide version: Google SRE describes "a company-wide annual disaster recovery
event called DiRT (Disaster Recovery Training) that combines theoretical and practical drills to perform
multiday testing of infrastructure systems and individual services"
(https://sre.google/sre-book/being-on-call/). The SRE book's *Emergency Response* chapter gives a real
test-induced-emergency postmortem: access to one MySQL database was deliberately blocked, and dependent
services across the company failed unexpectedly. What went well — "immediately aborted the exercise" on
recognising the scope; permissions restored within an hour; "Follow-up action items were resolved quickly
and thoroughly." What went wrong — "insufficient understanding of this particular interaction among the
dependent systems"; the team "failed to follow the incident response process" because it had not been
communicated; and rollback was flawed because "we hadn't tested our rollback procedures in a test
environment." The chapter's thesis: "Until your system has actually failed, you don't truly know how that
system, its dependent systems, or your users will react."
https://sre.google/sre-book/emergency-response/

**Caveat:** Kripa Krishnan's ACM Queue article "Weathering the Unexpected" (the primary DiRT write-up,
queue.acm.org/detail.cfm?id=2371516) returned HTTP 403; DiRT's rules of engagement and test-plan template
are therefore **unverified** here.

AWS Well-Architected's game-day structure (https://docs.aws.amazon.com/wellarchitected/latest/reliability-pillar/testing-reliability.html):
prepare (define realistic failure scenarios, objectives and success criteria, brief participants) → execute
(inject the failure; teams respond using standard procedures; observe and document actions; measure
response time) → post-game (retrospective, lessons learned, **update runbooks**, identify improvements).
Recorded: response times, MTTR, communication logs, steps taken and decisions made, issues discovered
(process gaps, documentation deficiencies, tool limitations), improvements committed.

The key inversion, and the direct answer to the owner's question: **in a game day the "user" being tested
is the on-call team, and the system under test is the runbook.** The code may be entirely innocent; the
defect being hunted is in the procedure and in the humans' model of the system.

### 3.6 Disaster recovery drills and runbook validation

DR drills measure two declared numbers: **RTO** (recovery time objective) and **RPO** (recovery point
objective). The drill passes if actual recovery beats the declared RTO and data loss stays within RPO.
Recommended practice: quarterly or semi-annual real failover, DNS failover validation, cross-region
replication verification, client-side behaviour during failover, and scheduled automated failover tests
monitored against RTO/RPO with alerting on deviation. Same AWS source as above.

Runbook validation has its own exercise format: **Wheel of Misfortune**. A game master prepares a scenario
(often a historical outage), two engineers sit at the front as primary and secondary on-call, and over
30–60 minutes they respond to a simulated page. The GM supplies information as the scenario unfolds, plays
other teams when escalation happens, and steers participants off red herrings. The rest of the team
watches. "When your disaster RPG is successful, everyone will have learned something."
https://sre.google/sre-book/accelerating-sre-on-call/

This is the cleanest existing example of **"an AI plays the scenario first, then a person does it"** in the
infrastructure world — except the GM is human and the point is training, not verification. The format
transfers directly: a scripted scenario, a fixed clock, an information-releasing referee, and a written
record of what the responder did and where the runbook failed them.

### 3.7 Production Readiness Review (Google SRE)

"A PRR is considered a prerequisite for an SRE team to accept responsibility for managing the production
aspects of a service." Six phases:

1. **Engagement** — 1–3 SREs open discussion with the dev team; establish an SLO/SLA for the service; plan
   necessary design changes; "common agreement about the process, end goals, and outcomes"
2. **Analysis** — against a per-service checklist: "The checklist is specific to the service and is
   generally based on domain expertise, experience with related or similar systems, and best practices from
   the Production Guide." SREs also review "recent incidents and postmortems for the service"
3. **Improvements and refactoring** — prioritise, negotiate, and *jointly implement*: "Both SRE and product
   development teams participate and assist each other in refactoring"
4. **Training** — design overviews, request-flow walkthroughs, production-setup description, "Hands-on
   exercises for various aspects of system operations"
5. **Onboarding** — "progressive transfer of responsibilities and ownership"; the dev team must remain
   available to back up SRE for a period
6. **Continuous improvement** — lessons from incidents and postmortems fold back into the Production Guide

https://sre.google/sre-book/evolving-sre-engagement-model/

The companion artefact is the **Launch Coordination Engineering launch checklist**
(https://sre.google/sre-book/launch-checklist/), ten sections: Architecture; Machines and datacenters;
Volume estimates, capacity and performance; System reliability and failover; Monitoring and server
management; Security; Automation and manual tasks; Growth issues; External dependencies; Schedule and
rollout planning. A representative question, verbatim: *"Are all user-visible request failures well
instrumented and monitored, with suitable alerting configured?"*

Two things to steal: **the checklist is per-service, not universal** (it is "specific to the service"), and
**it is seeded from that service's own incident history.** A card-level QA checklist assembled from the
project's own past bugs is the same construction.

### 3.8 Migration dry runs, shadow traffic, dark launches

- **gh-ost** (GitHub's online schema migration tool, https://github.com/github/gh-ost): captures changes
  from the binary log rather than triggers, applies them asynchronously to a "ghost" table, then cuts over.
  Its QA affordances are the interesting part: a **noop/dry-run** migration validating the change is sound;
  **`--test-on-replica`**, which runs the *entire* migration on a replica without swapping the table,
  leaving the original and the migrated copy side by side for **checksum comparison**; and
  `--postpone-cut-over-flag-file`, which lets a human choose the moment of the cut-over. That is a complete
  QA shape for a migration: dry run → replica rehearsal → row-level differential → human-timed cut-over.
- **Traffic mirroring / shadowing** (Istio, https://istio.io/latest/docs/tasks/traffic-management/mirroring/):
  a copy of live traffic is sent to the new version "out of band of the critical request path," as **fire
  and forget** — responses from the mirrored destination are discarded. `mirrorPercentage` controls the
  fraction. The new version sees real production traffic while being unable to affect any user.
- **Scientist** (GitHub, https://github.com/github/scientist): the in-process version. `use` is the
  **control** (the existing code), `try` is the **candidate**; both run in randomised order; results are
  compared with `==` (or a custom `compare` block); the candidate's exceptions are swallowed but recorded;
  **the control's value is always what gets returned.** The published `Scientist::Result` carries timing
  for both paths, match/mismatch/ignore counts, and full mismatch details.

```ruby
science "widget-permissions" do |experiment|
  experiment.use { model.check_user(user).valid? }   # control
  experiment.try { user.can?(:read, model) }         # candidate
end
```

All three are the same idea: **run the new thing against reality with its output disconnected, and let the
old thing be the oracle.** For a refactor card, this is a far better QA shape than any staged scenario —
the oracle is the previous implementation and the scenario is production itself.

### 3.9 Database migration verification, data-pipeline verification

Beyond gh-ost's checksums, the data-side equivalent of a unit test is a declarative data assertion. dbt's
model (https://docs.getdbt.com/docs/build/data-tests) is the most legible:

- **generic tests**: `unique`, `not_null`, `accepted_values`, `relationships` (referential integrity)
- **singular tests**: a SQL query; *"if a test returns zero rows, it passes"* — the failing rows *are* the
  failure report
- **severity**: `error` blocks the pipeline, `warn` does not; plus `error_if` / `warn_if` thresholds
- best practice: "Every model should have a data test on its primary key"

```yaml
models:
  - name: orders
    columns:
      - name: order_id
        data_tests: [unique, not_null]
      - name: status
        data_tests:
          - accepted_values: { arguments: { values: ['placed','shipped','completed','returned'] } }
      - name: customer_id
        data_tests:
          - relationships: { arguments: { to: ref('customers'), field: id } }
```

Two borrowings: **severity as a first-class property of a check** (a QA checklist line that warns vs one
that blocks), and **"the failing rows are the report"** — the evidence is the counterexample, not a boolean.

---

## 4. Correctness-heavy components

Every technique in this section is the same move: **make the adverse situation cheap to generate, and make
a machine the judge.** They differ only in what generates the situation and what judges it.

### 4.1 Formal specification and model checking — TLA+ at AWS

Chris Newcombe et al., "Use of Formal Methods at Amazon Web Services" (29 September 2014):
https://lamport.azurewebsites.net/tla/formal-methods-amazon.pdf

The motivating claim, worth quoting in full because it is the argument for this whole section:

> "We use deep design reviews, code reviews, static code analysis, stress testing, fault-injection testing,
> and many other techniques, but we still find that subtle bugs can hide in complex concurrent
> fault-tolerant systems. One reason for this problem is that human intuition is poor at estimating the
> true probability of supposedly 'extremely rare' combinations of events in systems operating at a scale of
> millions of requests per second."

The results table, verbatim from the paper ("Applying TLA+ to some of our more complex systems"):

| System | Components | Line count (excl. comments) | Benefit |
|---|---|---|---|
| S3 | Fault-tolerant low-level network algorithm | 804 PlusCal | Found 2 bugs. Found further bugs in proposed optimizations. |
| S3 | Background redistribution of data | 645 PlusCal | Found 1 bug, and found a bug in the first proposed fix. |
| DynamoDB | Replication & group-membership system | 939 TLA+ | Found 3 bugs, some requiring traces of 35 steps |
| EBS | Volume management | 102 PlusCal | Found 3 bugs. |
| Internal distributed lock manager | Lock-free data structure | 223 PlusCal | Improved confidence. Failed to find a liveness bug as we did not check liveness. |
| Internal distributed lock manager | Fault tolerant replication and reconfiguration algorithm | 318 TLA+ | Verified an aggressive optimization. |

Other verified facts from the paper: TLA+ used on **10 large complex real-world systems**, **7 teams** using
it at the time of writing, and "Engineers from entry level to Principal have been able to learn TLA+ from
scratch and get useful results in **2 to 3 weeks**."

The methodological point, which is the deepest idea in this report: the paper contrasts the ad-hoc
**"what might go wrong?"** approach (enumerate failure scenarios from experience, stop when imagination
runs out) with the formal **"what needs to go right?"** approach (state safety properties — "at all times,
all committed data is present and correct" — and liveness properties — "whenever the system receives a
request, it must eventually respond" — then let the checker enumerate). *"We have found this rigorous 'what
needs to go right?' approach to be significantly less error prone than the ad hoc 'what might go wrong?'
approach."*

**For a QA feature:** a card's QA section written as "what needs to go right" (an invariant) generalises;
one written as "what might go wrong" (a scenario list) does not. The 35-step DynamoDB counterexample is the
proof — no human writes a 35-step scenario.

### 4.2 Proof — seL4, CompCert

**seL4** (https://sel4.systems/About/FAQ.html, https://sel4.systems/Verification/): "the implementation is
proved to be free of implementation defects with respect to the specification"; on ARM and RISC-V64 "there
is a further proof that the binary code which executes on the hardware is a correct translation of the C
code"; and proofs that the specification "will enforce integrity and confidentiality." Functional
correctness implies "freedom from buffer overflows, null pointer exceptions, use-after-free, etc." The
seL4 site claims the proofs "go beyond what these certification schemes require" (Common Criteria,
ISO 26262, DO-178C) "for software development at their most stringent levels."

The **assumptions** list is the part worth copying, because it is an explicit statement of *where the
verification stops*: "The few lines of in-kernel assembly code are correct"; "Hardware behaves correctly";
"In-kernel hardware management (TLB and caches) is correct"; "Boot code is correct"; DMA off or provided by
trusted drivers; plus stated conditions on how the system is configured. Their own framing is that this is
where "manual validation and scrutiny can focus" — i.e. **the assumptions list is the human QA list.**

**Caveat:** I could not retrieve the proof-effort figures (person-years, lines of Isabelle) from a primary
source in this session.

**CompCert** (https://compcert.org/motivations.html) — the semantic preservation theorem, verbatim:

> "For all source programs S and compiler-generated code C, if the compiler, applied to the source S,
> produces the code C, without reporting a compile-time error, then the observable behavior of C is one of
> the possible observable behaviors of S."

And the empirical corroboration, which is also the best example of differential testing in existence
(Yang, Chen, Eide, Regehr, PLDI 2011, quoted on compcert.org):

> "The striking thing about our CompCert results is that the middleend bugs we found in all other compilers
> are absent… CompCert is the only compiler… for which Csmith cannot find wrong-code errors."

— after about **six CPU-years** of differential testing.

### 4.3 Property-based testing

Hypothesis (https://hypothesis.readthedocs.io/en/latest/): "you write tests which should pass for all
inputs in whatever range you describe, and let Hypothesis randomly choose which of those inputs to check."
Components: **strategies** (input domains), `@given` (bind strategy to test), **falsifying examples**,
**shrinking** to a minimal reproducer, an **example database** that replays previously-found failures, and
**stateful / rule-based testing** for systems with state transitions.

The oracle in property-based testing is the assertion — typically a *relation* rather than a value:
`assert set(lst) == set(result)` (a metamorphic property), or a round-trip (`decode(encode(x)) == x`), or
agreement with a reference implementation. Note that the example database makes every past failure a
permanent regression test with no human writing it down.

### 4.4 Fuzzing

OSS-Fuzz (https://google.github.io/oss-fuzz/) — launched 2016 after Heartbleed, combining "modern fuzzing
techniques with scalable, distributed execution." Engines: libFuzzer, AFL++, Honggfuzz, Centipede. The
oracle is a **sanitizer** (ASan/MSan/UBSan) plus any in-target assertion: the fuzzer does not know what
correct output is, only what *undefined behaviour* looks like. ClusterFuzz is "a distributed fuzzer
execution environment and reporting tool." Google "privately alerts developers to the bugs detected."
Reported impact as of August 2023: "over 10,000 vulnerabilities and 36,000 bugs across 1,000 projects."

### 4.5 Deterministic simulation testing

**FoundationDB** (https://apple.github.io/foundationdb/testing.html) — the reference implementation of the
idea. A single-threaded simulator built on the Flow actor language runs an entire cluster deterministically,
so a failing run "can perfectly repeat test runs, enabling controlled experiments to isolate issues." It
steps simulated time at roughly **10:1** compression. Modelled faults: drive performance, space limits and
capacity exhaustion; packet delivery and degradation; connection drops, machine slowdowns, shutdowns,
reboots, and datacenter-level failures. Bespoke adversaries like **"swizzle-clogging"** (randomly stopping
and restarting network connections) exist to find rare interleavings. Scale: "tens of thousands of
simulations every night," an estimated trillion CPU-hours cumulatively. The oracle is a workload invariant
— e.g. a *cycle test* that maintains ring-structured data and checks transactional isolation holds.

**TigerBeetle** (https://docs.tigerbeetle.com/concepts/safety/) — the **VOPR** simulator runs a whole
cluster of real production code at accelerated speed, "on 1024 cores, fuzzing the latest version of the
database" at ~1000× speed, injecting network faults, disk corruption and latency, and process crashes.
"This simulation can find both logical errors in the algorithms and coding bugs in the source." Judgment is
by assertion: the system "always runs with online verification on, to detect any discrepancies in the
data," and all data structures are "checksummed, and hash-chained."

**Antithesis** (https://antithesis.com/docs/introduction/how_antithesis_works/) — the commercial,
language-agnostic version: a deterministic hypervisor, autonomous exploration guided by reinforcement
learning toward unexplored states, and branching "multiverse" timelines from each fault-injection point,
so one test run produces thousands of alternate histories. The customer writes **property-based assertions**
("You directly state how your system should *ought* to behave" — e.g. "the database always returns the last
write"); Antithesis scans the multiverse for timelines where an assertion fails and returns a
**reproducible trace**.

### 4.6 Jepsen — adversarial black-box verification

https://jepsen.io/analyses — over two dozen databases, coordination services and queues analysed since
2013, finding "replica divergence, data loss, stale reads, read skew, lock conflicts, and much more."
Three properties: **opaque-box** testing of "real binaries running on real clusters" so that "bugs [are]
observable in production, not theoretical"; distributed-systems failure modes (faulty networks,
unsynchronised clocks, partial failure) rather than a clean environment; and **generative testing** —
"construct random operations, apply them to the system, and construct a concurrent history of their
results," then check that history "against a *model* to establish its correctness."

The judge is a consistency checker (Knossos for linearizability, Elle for transactional anomalies). The
report is a public written analysis naming the anomaly, the fault schedule that produced it, and the
history excerpt that witnesses it. **A Jepsen report is the gold standard of "evidence recorded": the
reproducible adverse schedule plus the counterexample history.**

---

## 5. Where humans still sit

Across all five areas, the human judgments that are never delegated fall into five kinds.

**(a) Is the requirement the right requirement?** Verification has a mechanical oracle; validation does
not. NASA's own split ("was the right end product realized?") puts this outside the reach of any test. Only
a stakeholder can say the spec was wrong.

**(b) Independent review of *artefacts*, not behaviour — code and design inspection.** The **Fagan
inspection** is the formalised version (M. E. Fagan, "Design and code inspections to reduce errors in
program development," *IBM Systems Journal* 15(3), 1976, 182–211;
https://en.wikipedia.org/wiki/Fagan_inspection). Roles: **moderator** (runs it, verifies rework),
**author**, **reader** (paraphrases the material aloud — *not* the author), **reviewer/tester**
(evaluates from a testing perspective), **recorder** (logs defects). Six stages: planning → overview →
preparation → inspection meeting → rework → follow-up, with a full re-inspection for non-trivial fixes.
The two rules that make it work: **defects are identified but never fixed in the meeting**, and defects
are logged and classified **major** (threatens function) vs **minor** (cosmetic). Reported effectiveness:
IBM and others claimed "80% to 90% of defects can be found and savings in resources up to 25%."

Why it cannot be delegated to the thing under test: the reader-is-not-the-author rule exists precisely
because the author cannot see their own assumption. DO-178C encodes the same rule as an objective-level
requirement ("Independence is required between code development and test case derivation… for software
level A and B").

**(c) Named, independent sign-off.** FAT is witnessed by the customer. SAT is run by the users. NASA runs a
separately funded IV&V organisation. DO-178C's SAS is signed. In every case the signature attests not "it
works" but "**I saw it, and I was not the one who built it.**" The independence is the content of the
signature.

**(d) Readiness gates — deciding whether to proceed at all.** TRR (are we ready to run this test?), ORR
(are we ready to operate?), PRR (is this production-ready?), SAR (do we accept delivery?). These are
judgments about the adequacy of the evidence and the residual risk, taken by people with the authority to
stop. Google's PRR is the software-native example and it is explicitly a prerequisite for accepting
operational responsibility — *someone is agreeing to carry a pager.* No automated check can make that
commitment.

**(e) Judging the *operators* and the procedures, not the code.** Game days, DiRT, Wheel of Misfortune.
Here the human is the system under test. Google's test-induced-emergency postmortem found three defects and
not one was in the code: an incomplete dependency model, an incident process that had not been
communicated, and an untested rollback procedure. Also in this bucket: the decision to **abort** an
experiment (Google "immediately aborted the exercise"), and the choice of the **cut-over moment** in a
migration (`--postpone-cut-over-flag-file`). Blast-radius judgment and go/no-go timing stay human.

**(f) The residual-assumption list.** seL4's assumptions ("hardware behaves correctly," "boot code is
correct," DMA is off or trusted) are, in their own framing, where "manual validation and scrutiny can
focus." Every automated verification has a boundary, and stating that boundary honestly — and judging
whether it is acceptable — is a human act. NASA requires this as a field: "an assessment of how the
verification environment differs from the operational conditions" (SWE-030).

---

## (a) Kind of work → oracle → staged situation → evidence → human signature

| # | Kind of work | Judge (oracle) | Staged situation that simulates the problem | Evidence recorded | What a person still signs |
|---|---|---|---|---|---|
| 1 | **Rate limiter** | Declared threshold under load: requests admitted ≤ limit, p95 latency bound, no starvation across tenants (k6/Gatling thresholds; SLO) | Load generator at 1×, 2×, 10× the limit; burst/spike profile; clock skew across limiter nodes; one node down mid-test | Threshold pass/fail per scenario, admitted-vs-offered curve, per-tenant fairness distribution, saturation point from a breakpoint test | That the chosen limit and fairness policy are the right *product* decision, and that the load profile resembles real traffic |
| 2 | **DB schema migration** | Row-level **checksum equality** between original and migrated table; replica lag bound; zero failed writes during cut-over | gh-ost `--test-on-replica`: full migration rehearsed on a replica without swapping, leaving both tables side by side; noop dry run first | Dry-run output, replica migration log, checksum comparison, cut-over duration, lag graph | The **cut-over moment** (`--postpone-cut-over-flag-file`), the rollback plan, and acceptance of the irreversible step |
| 3 | **Retry / backoff library** | Property-based invariants: total attempts ≤ max, delays within jitter bounds, monotonic backoff, budget never exceeded; plus a simulated-clock determinism check | Hypothesis/stateful test over failure sequences; deterministic simulation with injected timeouts, thundering-herd of N clients against one failing dependency | Falsifying examples + shrunk minimal reproducer, example database entries, herd-amplification factor measured | That the retry policy is appropriate for the *dependency's* failure semantics (idempotency) — a design judgment |
| 4 | **Motor / ECU controller** | Assertion on the plant model's state trajectory: current ≤ limit, no overshoot beyond spec, safe-state entered within N ms of fault | **HIL rig**: real-time plant model + emulated sensors/actuators driving the real controller; inject stuck sensor, open circuit, brownout, out-of-range command | Trace logs per scenario, structural coverage report (MC/DC at DAL A / ASIL D), fault-injection campaign results, traceability matrix | Test Readiness Review; the safety case; DO-178C/ISO 26262 objective sign-off **with independence** (not the author) |
| 5 | **Cache invalidation fix** | Differential: candidate vs control return the same value for the same key (Scientist); plus a staleness-bound assertion | Shadow/mirror production traffic to the new path fire-and-forget; concurrent write-then-read races under a deterministic scheduler | Match / mismatch / ignore counts, full mismatch details (both values, order, exceptions), timing for both paths | That the residual mismatch rate and the staleness bound are acceptable — a correctness/perf trade-off |
| 6 | **Parser** | (i) round-trip property `parse(print(x)) == x`; (ii) **differential** against a reference parser; (iii) sanitizers under fuzzing — no crash, no UB, no OOM | OSS-Fuzz-style continuous fuzzing from a seed corpus; adversarial inputs (deep nesting, truncation, encoding confusion); Csmith-style generated valid inputs | Crash reproducers, corpus + coverage growth, differential disagreement cases, regression corpus entries | Whether a disagreement with the reference is a bug *in ours* or *in theirs* — and the security disclosure decision |
| 7 | **CI pipeline change** | The pipeline reproduces a known-good build bit-for-bit (reproducible-build check) and still fails on a known-bad commit | Replay the change against a fixed set of historical commits: N known-good (must pass) and M known-bad (must fail); a deliberately broken commit as a negative control | Pass/fail matrix over the replay set, build times, cache hit rate, an artefact-hash diff against the previous pipeline | That the pipeline's *gates* still encode what the team wants blocked; nobody signs off a pipeline that cannot fail |
| 8 | **Terraform module** | `assert { condition … error_message … }` in `.tftest.hcl`; plan-mode assertions for logic, apply-mode against real (or mocked) providers; policy-as-code (OPA/Sentinel) | `run` blocks that apply the module into a throwaway account with edge-case variables; a destroy/re-apply cycle; drift injected by hand then re-planned | Test run output per `run` block, the plan diff, policy evaluation result, cost estimate | The **production apply**: reading the plan diff and approving destructive changes (replacement of stateful resources) |
| 9 | **Consensus / replication change** | (i) TLA+ safety and liveness properties model-checked; (ii) a linearizability checker over a generated concurrent history (Jepsen/Elle) | Model checker enumerates all interleavings; then a real cluster under partitions, clock skew, process kills, disk corruption — or a deterministic simulator (FDB/VOPR/Antithesis) | Model-checker counterexample traces (AWS saw one needing **35 steps**), the failing history + fault schedule, simulation seed for exact replay | That the modelled environment's assumptions match reality (what the model assumes a disk/network can do) |
| 10 | **Data pipeline stage** | Declarative data assertions: `unique`, `not_null`, `accepted_values`, `relationships`; row-count and distribution deltas vs the previous run; reconciliation against source of truth | Run against a frozen historical slice with known-bad records deliberately present; late-arriving and duplicate events; a schema change upstream | The **failing rows themselves** (a test that returns zero rows passes), severity per check (`warn` vs `error`), freshness and volume metrics | That a `warn`-severity anomaly is acceptable this run, and any backfill/replay decision |
| 11 | **Public API / schema change** | `buf breaking --against '.git#branch=main'` at the chosen category (WIRE / WIRE_JSON / PACKAGE / FILE); Pact provider verification for every known consumer | Replay every consumer's recorded pact against the new provider build; deserialize previously-serialized payloads | Breaking-change report, pact verification matrix, `can-i-deploy` output and exit code | The **deprecation policy and timeline** for an intentional break, and the choice of strictness category |
| 12 | **Compiler / codegen change** | Differential testing by majority vote across compilers on randomly generated programs (Csmith); or a semantic-preservation proof (CompCert) | Millions of generated valid programs compiled at every optimisation level and executed; outputs compared | Miscompilation reproducers, CPU-hours invested (Yang et al.: ~6 CPU-years), the proof script if verified | That the reduced test case is genuinely undefined-behaviour-free before it is filed as a bug |
| 13 | **Failover / DR mechanism + runbook** | Measured recovery time vs declared **RTO**, data loss vs declared **RPO**; steady-state metrics recover to baseline | **Game day / DiRT**: sever a region, kill the primary, revoke a credential; the on-call team responds using only the written runbook | Timeline of actions and decisions, MTTR, communication log, gaps found in the runbook, whether the abort criterion was hit | The go/no-go to run it, the blast-radius limit, the **abort call**, and the retrospective's action items |
| 14 | **Deploy of any service change** | Automated canary analysis: per-metric Mann-Whitney U at 98% confidence → Pass/High/Low/Nodata; group score = pass/total × 100; weighted summary vs pass & marginal thresholds | Route 5% of live traffic to the canary beside an identical control; hold for ≥ one metric collection interval | Canary report: per-metric classification, per-group scores, summary score, the thresholds applied, the promote/abort/inconclusive verdict | Only the **Inconclusive** case — by design. Automated analysis "collect[s] the measurement, but still allow[s] human judgement" |
| 15 | **A whole new service going live** | The PRR checklist — service-specific, seeded from that service's own incidents and from the Production Guide | Load test to capacity limits; simulated machine/rack/cluster loss; dependency failure; a staged rollout | Completed checklist with an answer per item, SLO/SLA agreed, prioritised improvement list, training materials | The SRE team's acceptance of operational ownership — i.e. someone agreeing to carry the pager |

---

## (b) Terms and artefact formats worth borrowing verbatim

### B1. Verification method code — T / A / I / D
Every requirement gets a declared method *before* verification starts. **Real example (NASA SE Handbook
§5.3):** "Test — The use of an end product to obtain detailed data needed to verify performance…"; "Analysis
— mathematical modeling and analytical techniques…"; "Inspection — The visual examination of a realized end
product"; "Demonstration — showing that the use of an end product achieves the individual specified
requirement." https://www.nasa.gov/reference/5-3-product-verification/

**Borrow as:** a one-letter tag on every line of a card's QA section. `[T]` produces a number, `[D]` produces
"it happened", `[I]` is a review of an artefact, `[A]` is an argument from other evidence. The tag alone
tells the verifier what kind of evidence is expected and whether an agent can produce it.

### B2. Verification / traceability matrix row
```
Req ID | Requirement text | Method (T/A/I/D) | Level | Procedure ID | Result ref | Status
```
**Real example:** NASA §5.3's listed outputs include "Compliance/verification matrices tracing requirement
satisfaction," and SWE-030 requires results be "record[ed], address[ed], and track[ed] to closure."
Bidirectional traceability is mandatory in ISO 26262 Part 6.
**Borrow as:** every QA-checklist line points at a card requirement, and every card requirement has at least
one QA line. Unverified requirements and orphan tests are both defects in the card.

### B3. Acceptance test procedure — the step format
```
Step | Action | Expected result | Actual result | Pass/Fail | Witness | Notes / anomaly ref
  1  | Set supply to 24 V, enable | Output current ramps to 12.0 A ±0.2 A within 50 ms | 11.94 A, 41 ms | P | EA | —
  2  | Open sensor feed line       | Controller enters SAFE within 100 ms, fault code 0x2A | SAFE at 87 ms, 0x2A | P | EA | —
  3  | Restore feed line           | No auto-restart without operator reset | auto-restarted | F | EA | NCR-114
```
Preceded by: test article ID and configuration, environment and how it differs from operational conditions,
prerequisites, safety and **abort criteria**. Followed by: anomaly/non-conformance reports and a punch list.

**Real example:** this is IEEE 829's split made into one table. IEEE 829 defines a *Level Test Procedure
(LTPr)* that "details execution steps, including setup preconditions," a *Level Test Log (LTL)* to "provide
a chronological record of relevant details about the execution of tests," and an *Anomaly Report (AR)*
covering "actual/expected results and supporting evidence"
(https://en.wikipedia.org/wiki/Software_test_documentation; superseded by ISO/IEC/IEEE 29119-3:2013). The
**Witness** column comes from FAT practice — the customer's representative initials each step at the
supplier's site before shipment (https://en.wikipedia.org/wiki/Factory_acceptance_testing).

**Caveat:** I could not retrieve a verbatim public FAT/ATP document in this session; the column set above is
assembled from IEEE 829's document definitions plus FAT witnessing practice, not copied from one source.

**Borrow as:** this is the exact shape of a "Human QA" section. Note what it does and does not ask: it never
asks "does this feel right?", it asks "do the actual results match the expected results that were written
down before you started?" The *expected result column is written before the run* — that is what makes it a
test rather than an observation. And the Witness column is the independence record.

### B4. Test Readiness Review checklist
Entry gate before any staged run: procedures written and **approved**; test article configuration identified
and under configuration control; environment ready and its differences from operational conditions assessed;
personnel trained; pass criteria and **abort criteria** agreed in advance; known open anomalies dispositioned.

**Real example:** NASA NPR 7123.1D, Appendix G, Table G-10 (Test Readiness Review), alongside G-8 (PRR),
G-11 (SAR), G-12 (ORR). https://nodis3.gsfc.nasa.gov/displayDir.cfm?t=NPR&c=7123&s=1D

**Borrow as:** the step between "a card has a QA section" and "someone runs it." If the expected results and
the abort condition are not written down first, the run is not evidence.

### B5. Game-day plan
```
Scenario name / hypothesis (steady state that should hold)
Blast radius and abort criteria       Participants and roles (GM, primary, secondary, observers)
Fault to inject, at T+0               Information the GM releases, and when
Expected detection path (which alert) Expected mitigation (which runbook)
Timeline log: T+mm  who  action  observation
Findings: gaps in system / runbook / process     Owners and dates
```
**Real example:** Google's test-induced emergency — blocking access to one MySQL database — recorded exactly
these: the abort ("immediately aborted the exercise"), the detection gap ("insufficient understanding of
this particular interaction among the dependent systems"), the process gap ("failed to follow the incident
response process" because it had not been communicated) and the procedure gap ("we hadn't tested our
rollback procedures in a test environment"). https://sre.google/sre-book/emergency-response/
The scripted variant with a referee is **Wheel of Misfortune**: a GM, two on-callers at the front,
30–60 minutes, scenario often drawn from a past outage, GM plays other teams and steers off red herrings.
https://sre.google/sre-book/accelerating-sre-on-call/

**Borrow as:** this is the closest existing artefact to "put a person into a staged scenario." Its lesson for
Relay: the subject under test is the *runbook and the responder's model*, and the finding is a gap, not a
pass/fail.

### B6. Production Readiness Review checklist
Ten sections, per-service, seeded from that service's own incidents: Architecture; Machines and datacenters;
Volume estimates, capacity and performance; System reliability and failover; Monitoring and server
management; Security; Automation and manual tasks; Growth issues; External dependencies; Schedule and
rollout planning.

**Real example, verbatim question:** *"Are all user-visible request failures well instrumented and monitored,
with suitable alerting configured?"* — Google SRE, PRR Analysis phase, where "The checklist is specific to
the service and is generally based on domain expertise, experience with related or similar systems, and best
practices from the Production Guide."
https://sre.google/sre-book/launch-checklist/ and https://sre.google/sre-book/evolving-sre-engagement-model/

**Borrow as:** a project-level checklist that cards inherit from, *assembled from the project's own closed
bug cards*. That is exactly Google's construction and it is the cheapest way to make a QA checklist
project-specific rather than generic.

### B7. Canary analysis report
```
Canary config: baseline=v1.4.2 canary=v1.4.3  traffic=5%  window=30m  interval=5m
Metric group "latency"  weight 40
  request_p95       Mann-Whitney U, 98% conf, tolerance ±0.25·HL, allowedIncrease 5%   → Pass
  request_p99       …                                                                  → High
  group score 50
Metric group "errors"   weight 60
  http_5xx_rate     …                                                                  → Pass
  exception_count   …                                                                  → Nodata
  group score 50
Summary score 50   (pass threshold 75, marginal threshold 50)  → MARGINAL
Verdict: paused for human judgement
```
**Real example:** the NetflixACAJudge in Spinnaker classifies each metric Pass / High / Low / Nodata /
NodataFailMetric using a Mann-Whitney U test where "the judge needs to be 98% confident there's a real
difference before flagging a metric"; group score = "(Pass count / Total count) × 100"; the summary score is
"a weighted average of group scores" compared against pass and marginal thresholds.
https://spinnaker.io/docs/guides/user/canary/judge/
The declarative equivalent, with the three-valued outcome: Argo Rollouts' `successCondition` /
`failureCondition` / `failureLimit`, where an **Inconclusive** result "pauses the rollout, requiring human
judgment," enabling "automation of analysis runs, and collect the measurement, but still allow human
judgement." https://argo-rollouts.readthedocs.io/en/stable/features/analysis/

**Borrow as:** the QA-checklist result type. Not a boolean — **pass / fail / inconclusive**, with a score, the
thresholds that were applied, and an explicit escalation to a person *only* on inconclusive.

### B8. `can-i-deploy` matrix
```
CONSUMER | C.VERSION | PROVIDER | P.VERSION | SUCCESS? | RESULT#
Foo      | 24        | Bar      | 56        | false    | 1
(exit code 1 means no)
```
**Real example:** https://docs.pact.io/pact_broker/can_i_deploy
**Borrow as:** a card that changes an interface is not "done" when its own checks are green; it is done when
the matrix over (this version × every dependent's version × target environment) is green.

### B9. FMEA row as the *source* of the staged scenario
```
Item/Function | Failure mode | Effect (local/next/system) | Cause | S | O | D | RPN | Current control | Recommended action
```
**Real example:** SAE J1739 / IEC 60812 worksheets; "FMEA results directly inform verification test plans,
ensuring critical failure modes receive appropriate validation coverage and detection capability assessment."
https://en.wikipedia.org/wiki/Failure_mode_and_effects_analysis

**Borrow as:** the **Detection** column *is* a QA line, and the failure mode *is* the scenario to stage. For a
bug card this is free: the bug that occurred is a failure mode with an observed effect and a known cause; the
staged scenario is its cause chain, and the QA line is the detection control that should have caught it.

### B10. Residual-assumptions statement
A verification is only as good as what it assumed. **Real example (seL4):** "The few lines of in-kernel
assembly code are correct"; "Hardware behaves correctly"; "In-kernel hardware management (TLB and caches) is
correct"; "Boot code is correct"; DMA off or provided by trusted drivers. https://sel4.systems/About/FAQ.html
Reinforced by NASA SWE-030, which requires recording "an assessment of how the verification environment
differs from the operational conditions." https://swehb.nasa.gov/display/SWEHBVB/SWE-030+-+Verification+Results

**Borrow as:** a mandatory field on any staged-scenario QA result — *what the stage did not reproduce.* This
is the field that converts a green staged run into an honest claim, and it is a natural place for a genuine
"Human QA" question: "the stage used a stub provider and a 50 ms fixed latency; is that close enough for you
to believe the fix?"

### B11. "What needs to go right" vs "what might go wrong"
**Real example (AWS):** "We have found this rigorous 'what needs to go right?' approach to be significantly
less error prone than the ad hoc 'what might go wrong?' approach," where safety properties are phrased "at
all times, all committed data is present and correct" and liveness properties "whenever the system receives a
request, it must eventually respond."
https://lamport.azurewebsites.net/tla/formal-methods-amazon.pdf

**Borrow as:** prompt the QA section for an *invariant* first and a scenario list second. Scenario lists stop
where imagination stops; the DynamoDB bug needed a 35-step trace that no human would have written.

### B12. The independence rule, stated as a rule
**Real example (DO-178C, via AdaCore):** "Independence is required between code development and test case
derivation, to satisfy the independence criteria of objectives A6-3 and 4 for software level A and B."
https://learn.adacore.com/booklets/adacore-technologies-for-airborne-software/chapters/analysis.html
Reinforced by the Fagan inspection's reader-is-not-the-author rule and by NASA's separately funded IV&V
organisation.

**Borrow as:** **the agent that implemented a card may not be the agent that verifies it, and may not be the
agent that writes its QA checklist.** This single rule is what three separate industries independently
converged on, and it is cheap to encode in a Board.

---

## What I could not verify

- **DO-178C objective counts** (71/69/62/26 per DAL A/B/C/D, and the ~30-with-independence figure at Level A).
  RTCA DO-178C is paywalled; I found these only in secondary literature and vendor pages.
- **ISO 26262 Part 6 per-ASIL structural coverage table.** LDRA summarises it as "MC/DC at ASIL C and D";
  the standard's tables are paywalled and I could not confirm the exact recommendation marks.
- **Google DiRT rules of engagement / test-plan template.** Kripa Krishnan, "Weathering the Unexpected,"
  ACM Queue (queue.acm.org/detail.cfm?id=2371516) returned HTTP 403. DiRT's existence and shape are confirmed
  from the Google SRE book only.
- **Netflix's own Kayenta announcement** (netflixtechblog.com) returned HTTP 403; the Kayenta judge details
  above come from spinnaker.io docs instead.
- **NASA NPR 7123.1D Appendix G full entrance/success criteria text** for TRR/SAR/ORR — I confirmed the table
  numbers from the contents page but the body pages did not render.
- **seL4 proof effort** (person-years, lines of Isabelle) — not retrievable from the pages I could reach.
- **NASA IV&V's technical/managerial/financial independence triad** — the standard formulation, but the NASA
  IV&V page I reached did not state it.
- **IEC 62381** (FAT/SAT/SIT for process-industry automation) — the IEC webstore URL I tried served a
  different standard; the reference is unverified.
- **FDA General Principles of Software Validation body text** — I confirmed the title, date (11 Jan 2002) and
  PDF link but read only the landing page.
- **A verbatim public acceptance test procedure.** The step-format table in B3 is assembled from IEEE 829's
  document definitions plus FAT witnessing practice; it is not a transcription of one real document.
