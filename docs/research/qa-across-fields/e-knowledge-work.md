# QA for scientific, research and knowledge work

*Research report, 2026-09-21. Companion to the quantitative-analysis report; that one covers
replication packages, Monte Carlo with planted truth, reference datasets, dbt/Great Expectations,
ML evaluation and many-analysts studies, and is not repeated here.*

Scope: how quality is assured in the knowledge-work professions that already do this at scale —
evidence synthesis, editing and fact-checking, model audit and financial audit, legal and analytic
tradecraft, laboratory science — and how AI agents doing the same work are currently evaluated.
The organising question throughout is the one a QA-support feature has to answer per card:
**who or what is the judge, what can be staged with a known answer, what can a machine settle
first, and what is left that only a person can decide.**

Method note: the session's web-search budget was exhausted early, so everything here was obtained by
fetching primary documents directly (arXiv, ACL Anthology, NCBI E-utilities, NTRS, GitHub raw,
agency PDFs). Numbers are quoted from the source that established them. Where a widely repeated
figure could not be traced to a primary source, it is flagged as unverified rather than repeated.

---

## 1. Research synthesis and literature work

This is the most mature QA regime in knowledge work: a written standard for what must be disclosed
(PRISMA), a written standard for how the work must be conducted (Cochrane's MECIR), a judgement
artefact that carries its own evidence (risk of bias), a certainty grade with reasons attached
(GRADE), and — uniquely — **published measurements of what happens when you cut the second
reviewer.**

### 1.1 PRISMA 2020: the flow diagram is a reconciliation ledger

PRISMA 2020 (Page et al., *BMJ* 2021;372:n71) is a 27-item reporting checklist plus a flow diagram.
What makes the diagram interesting as an artefact is not that it is a picture; it is that **every
number must reconcile with the numbers on either side of it**. The official Word template
(prisma-statement.org) has these boxes verbatim:

```
Records identified from*:            Records removed before screening:
    Databases (n = )                     Duplicate records removed (n = )
    Registers (n = )                     Records marked as ineligible by automation tools (n = )
                                         Records removed for other reasons (n = )

Records screened (n = )           →  Records excluded** (n = )
Reports sought for retrieval (n = ) → Reports not retrieved (n = )
Reports assessed for eligibility (n = ) → Reports excluded:
                                              Reason 1 (n = )
                                              Reason 2 (n = )
Studies included in review (n = )
Reports of included studies (n = )
```

with two footnotes on every template, the second of which is new in 2020 and is squarely about AI:

> `**If automation tools were used, indicate how many records were excluded by a human and how many
> were excluded by automation tools.`

A real one that closes (Ermiati et al., *Int J Women's Health* 2026;18:1-8, doi:10.2147/IJWH.S616559):
PubMed 5 + Scopus 8 + ScienceDirect 19 + Garuda 15 = **47**; duplicates removed **6** → **41**
screened; excluded 18 + 1 → **22** sought; **4** not retrieved → **18** assessed; excluded 3 + 1 + 3
+ 1 → **10** included. 47 − 6 = 41; 41 − 19 = 22; 22 − 4 = 18; 18 − 8 = 10.

And a real one that does not, in a peer-reviewed journal (*Eur Psychiatry* 2026,
doi:10.1192/j.eurpsy.2026.11127): `Records identified from Databases (n = 3)`, `Duplicate records
(n = 399)`, `Records screened (n = 1,299)`, `Reports sought for retrieval (n = N A)`. Three records
in, 1,299 screened. **This is the cheapest, most valuable automated check in the entire report: the
arithmetic of a flow diagram is decidable, it is frequently wrong, and no human reviewer is checking
it.**

The three PRISMA items that are really QA rules all end the same way:

> **8. Selection process** — "Specify the methods used to decide whether a study met the inclusion
> criteria of the review, including **how many reviewers screened each record and each report
> retrieved, whether they worked independently**, and if applicable, **details of automation tools
> used in the process**."

Items 9 (data collection) and 11 (risk of bias) repeat the identical clause. PRISMA does not mandate
dual screening — it mandates **disclosure of how many people did it, whether independently, and what
machine helped**. That is exactly the shape a QA record for an agent-executed card should take.

### 1.2 Cochrane MECIR: which duplications are mandatory, and which are not

Cochrane's conduct standards are graded **Mandatory** or **Highly desirable**, and the grading is
the interesting part because it is a materiality judgement:

| # | Standard | Status |
|---|---|---|
| **C39** | "Use (at least) two people working independently to determine whether each study meets the eligibility criteria, and **define in advance the process for resolving disagreements**." | Mandatory |
| **C43** | "Use a data collection form which has been piloted." | Mandatory |
| **C45** | Two people, independently, to extract **study characteristics** | **Highly desirable** |
| **C46** | Two people, independently, to extract **outcome data** | **Mandatory** |
| **C53** | Two people, independently, to apply the risk-of-bias tool to **each result in each included study** | Mandatory |
| **C54** | "Justify judgements of risk of bias … and provide this information in the risk-of-bias tables (as **'Support for judgement'**)." | Mandatory |
| **C55** | "Collect the **source of information** for each risk-of-bias judgement (e.g. quotation, summary of information from a trial report, correspondence with investigator…)" | Mandatory |

C45's own rationale states the trade-off explicitly: "Dual data extraction may be less important for
study characteristics than it is for outcome data, so it is not a mandatory standard for the
former." C46's: "Dual data extraction is particularly important for outcome data, which feed
directly into syntheses of the evidence, and hence to the conclusions of the review." **That is
materiality, applied to review labour.** The expensive second pair of eyes goes where an error
propagates to the conclusion.

The Handbook draws the same line for screening (§4.6.4), and this single parenthesis is the whole
licence for single screening anywhere in evidence synthesis:

> "Ideally, screening of titles and abstracts to remove irrelevant reports should also be done in
> duplicate by two people working independently (**although it is acceptable that this initial
> screening of titles and abstracts is undertaken by only one person**). **It is essential, however,
> that two people working independently are used to make a final determination as to whether each
> study considered possibly eligible after title/abstract screening meets the eligibility criteria
> based on the full text**…"

Cochrane is also explicitly *hostile to agreement statistics as a quality measure*, which is worth
knowing before building a κ display. Handbook §7.3.2:

> "**We do not recommend the use of statistical measures of agreement (such as kappa statistics) to
> describe the extent to which assessments by multiple authors were the same. It is more important
> that reasons for any disagreement are explored and resolved.**"

Chapter 4 mentions kappa zero times. §5.5.5 permits it "only for the most important data". For
calibration, real published title/abstract κ runs low: Hechtman et al. (*J Hosp Med* 2026) report
"83.2% agreement (mean pairwise kappa, k = 0.43)" at title/abstract, rising to κ = 0.66 at full
text.

The disagreement procedure is a recorded artefact too, with a named terminal state for the
irresolvable case (§4.6.4): "Occasionally, it will not be possible to resolve disagreements…
authors may choose to categorize the study in their review as one that is **awaiting assessment**."
And §5.5.5: "**The presence and resolution of disagreements should be carefully recorded.
Maintaining a copy of the data 'as extracted' (in addition to the consensus data) allows assessment
of reliability of coding.**"

### 1.3 What the second reviewer is actually worth — measured

This is the only place in knowledge work I found with clean experimental estimates of the value of
maker-checker, and they are strikingly consistent:

| Study | Design | Finding |
|---|---|---|
| Gartlehner et al., *J Clin Epidemiol* 2020;121:20–28 | **RCT** on Cochrane Crowd; 280 participants, 24,942 decisions, 2,000 abstracts | "single-reviewer abstract screening **missed 13% of relevant studies** (sensitivity 86.6%)… dual-reviewer abstract screening **missed 3%** (sensitivity 97.5%)" |
| Edwards et al., *Stat Med* 2002;21:1635–1640 | 22,571 records, every record double-screened | "**Single reviewers missed on average 8 per cent of eligible reports (range 0 to 24 per cent), whereas pairs of reviewers did not miss any (range 0 to 1 per cent).**" Also: when both reviewers agreed a record was potentially relevant, 81% proved eligible; **when they disagreed, 22% were eligible** |
| Waffenschmidt et al., *BMC Med Res Methodol* 2019;19:132 | 23 single screenings, 9 reviewers | median missed **5% (range 0–58%)**; **3% for experienced reviewers, 13% for less-experienced** |
| Stoll et al., *Res Synth Methods* 2019;10:539–545 | 30,000 records | a second reviewer found an extra **6.6–9.1%** at title/abstract and **6.6–11.9%** at full text |
| Buscemi et al., *J Clin Epidemiol* 2006;59:697–703 | extraction | "Single data extraction resulted in more errors than double data extraction (**relative difference 21.7%, P = .019**)" at 36% more time |

Two things follow directly. First, **the disagreement set is the high-yield set** — Edwards' 22% vs
81% means the records the two reviewers *disagree* about are where the information is. Second,
the Cochrane Rapid Reviews Methods Group's recommendation R14/R15 is the best-value design found
anywhere in this report:

> **R14.** "Use two reviewers for dual screen of at least 20% (ideally more) of abstracts, with
> conflict resolution. • Use one reviewer to screen the remaining abstracts and **a second reviewer
> to screen all excluded abstracts**."
> **R15.** "Use one reviewer to screen all included full-text articles and **a second reviewer to
> screen all excluded full-text articles**."
> **R16.** "Use a single reviewer to extract data using a piloted form. Use a second reviewer to
> check for correctness and completeness of extracted data."

**One pass forward, a second pass over the rejects only.** The second reviewer is spent entirely on
false negatives, because a false positive surfaces at the next stage and a false negative never
does. For an agent doing a card, the analogue is: *don't re-check what the agent did; check what the
agent decided not to do.* (Note that "liberal accelerated" is Khangura et al., *Syst Rev* 2012, not
Cochrane; and "safety first" does not appear in the Handbook at all.)

### 1.4 Risk of bias: the judgement artefact worth copying wholesale

RoB 2 (Sterne et al., *BMJ* 2019;366:l4898; tool at riskofbias.info, version 22 August 2019) is the
best-designed judgement record I found in any field. Its structure:

- **Signalling questions** — factual, answered independently of one another. Response options,
  verbatim: "(1) Yes; (2) Probably yes; (3) Probably no; (4) No; (5) No information". The design of
  that scale is the point: "The definitive versions ('Yes' and 'No') would typically imply that firm
  evidence is available…; the 'Probably' versions would typically imply that **a judgement has been
  made**." And "No information" is tightly fenced: it is used "only when both (i) insufficient
  details are reported… and (ii) in the absence of these details it would be unreasonable to respond
  'Probably yes' or 'Probably no'".
- **A free-text support field next to *every* signalling question**, not just the domain verdict:
  "These boxes should be used to provide support for the answer to each signalling question.
  **Brief direct quotations from the text of the study report should be used whenever possible.**"
- **A second free-text field next to the domain judgement**, with an explicit override rule: "It is
  particularly important that reasons are provided for **any judgements that do not follow the
  proposed algorithms**." The algorithms produce *proposed* judgements that a human may overrule, in
  writing.
- **A domain verdict** — Low risk / Some concerns / High risk — and an overall verdict with a stated
  aggregation rule ("High" if any domain is High, **or** "some concerns for multiple domains in a
  way that substantially lowers confidence in the result").
- **A provenance checklist before any domain is scored**: which of journal article, trial protocol,
  statistical analysis plan, registry record, grey literature, regulatory document, or personal
  communication were consulted.
- **Per-assessor records kept, consensus records published** (§2.3): "review authors may wish to
  present the answers, free-text supports and judgements **for each assessor separately**… **Only
  consensus judgements across multiple assessors should be presented**" in the main document, with
  the per-assessor detail in an appendix.
- **An explicit position on early stopping** (§2.4): because one High domain forces an overall High,
  "users of the tool may be tempted to stop their assessment as soon as one domain is judged as
  'High'. **We discourage this**… We recommend that this be done only when it has been pre-specified
  in the protocol."

ROBINS-I (Sterne et al., *BMJ* 2016;355:i4919) applies the same machinery to non-randomised studies
with seven domains and five verdicts (Low / Moderate / Serious / Critical / No information), and
adds two rules worth stealing: "'Risk of bias' is to be interpreted as **'risk of material bias'**…
a serious risk of a very small degree of bias should not be considered 'Serious risk' of bias"; and
a *Critical* rating means the study "should not be included in any synthesis" — a verdict that is
also an action.

Cochrane's own assessment of machine-assisted RoB, verbatim from Handbook §7.3.2, is a useful
calibration for anyone building an AI checker for judgement-shaped work:

> "A study showed that **about one-third of articles could be assessed by just one reviewer** if such
> a tool is used instead of the two required reviewers (Millard et al 2016). However, **reliability
> in reaching judgements about risk of bias compared with human reviewers was slight to moderate
> depending on the domain assessed** (Gates et al 2018)."

### 1.5 GRADE: a certainty rating whose every downgrade carries a footnote

Four levels, verbatim from the GRADE Handbook:

- **High** — "We are very confident that the true effect lies close to that of the estimate of the effect."
- **Moderate** — "We are moderately confident… the true effect is likely to be close to the estimate… but there is a possibility that it is substantially different"
- **Low** — "Our confidence in the effect estimate is limited…"
- **Very Low** — "We have very little confidence in the effect estimate…"

Five downgrade domains (risk of bias, inconsistency, indirectness, imprecision, publication bias),
each costing one or two levels, classified as "not serious" / "serious" / "very serious" (and for
ROBINS-I-assessed non-randomised studies, "extremely serious" for three levels). Randomised evidence
starts High; non-randomised starts Low; nothing can fall below Very Low. And: "Quality of evidence
is a continuum; any discrete categorisation involves some degree of arbitrariness. Nevertheless,
advantages of simplicity, transparency, and vividness outweigh these limitations."

The reusable artefact is the **Summary of Findings** table, in which every downgrade carries a
numbered footnote saying *which* domain was downgraded and *why*. A certainty grade with no footnote
is not a GRADE rating.

### 1.6 Screening automation: the oracle, and the stopping rule

This is where evidence synthesis has already had the argument the rest of knowledge work is about to
have, and the answers are unflattering.

**The oracle is recall against a gold set of known includes.** Not accuracy — at a base rate of, say,
41 relevant in 16,669 records, accuracy is meaningless. The standard metric, **WSS@95%** (work saved
over sampling at 95% recall, Cohen et al. 2006), reports how much screening you avoid while still
finding 95% of what matters. Note that even its originator abandoned it (Cohen moved to AUC), and
Norman (2020) found it "strongly influenced by random effects and tends to have a large variance."
The 95% target itself is a **modelling assumption, not guidance**: Cohen 2006 says "For this study,
we assumed that a recall of 0.95 or greater was required."

**Stopping rules are the hard part, and the honest numbers are small.** Callaghan & Müller-Hansen
(*Syst Rev* 2020;9:273) built the first statistically principled criterion: model the unseen pile as
an urn, test H₀ that enough relevant records remain, stop when that becomes implausible at 95%
confidence for a 95% recall target. Across 21 datasets:

| Criterion | Mean work saved | 95%-recall target missed |
|---|---|---|
| Random-sampling hypergeometric | 15% | **3.29%** |
| Ranked quasi-sampling hypergeometric | **17%** | **0.95%** |
| Heuristic: 50 consecutive irrelevant | **41%** | **39%** |

**The popular heuristic saves more than twice the work and blows the recall target 39% of the
time.** The 2026 head-to-head (Repke et al., *Cochrane Evidence Synthesis and Methods* 2026,
doi:10.1002/cesm.70068) tested **15 stopping methods across 81 real datasets** and concluded:
"almost all existing stopping methods either fail to reliably stop without missing relevant records
or fail to utilize the full potential work-savings. **Only one method reliably meets the set recall
target, but stops conservatively.**" Consecutive-exclusion heuristics had mean achieved recall of
**34–35% (range 0–100)**. An ASReview-specific replication (Kempny et al., *BMC Med Res Methodol*
2026) ran 35,000 simulated screenings and found "**no stop criterion reliably ensures that all
relevant studies are identified**."

**What a serious deployment looks like.** Cochrane's RCT classifier (Thomas et al., *J Clin
Epidemiol* 2021;133:140–151) is calibrated **not to 95% but to 99% recall**, accepting **8%
precision** to get there (roughly 12 records passed per true RCT). Against Cochrane's own included
studies it "correctly identified 43,783 (99.5%) of 44,007 RCTs". Records scoring below the
99%-recall threshold "were automatically classified as not reporting an RCT" — auto-excluded, no
human — which saved **185,039 of 449,480 records** from manual checking in 2018. Everything *above*
the threshold goes to Cochrane Crowd, where the documented quorum rule (Wallace et al., *JAMIA*
2017) is: "**If only novices are involved, 3 consecutive consistent judgments are required. If at
least 1 screener is an expert, then only 2 consecutive agreements are required.**"

That is the whole design in one sentence: **a machine sieve calibrated to near-perfect recall, a
human quorum behind it, and auto-exclusion only below a threshold that was measured, not guessed.**

**LLM screening, 2024–2026, is not there yet.** Oami et al. (*JAMA Netw Open* 2024) ran GPT-4 Turbo
over 16,669 publications across 5 clinical questions with 41 finally selected: sensitivity **0.75
(95% CI 0.43–0.92)**, specificity 0.99, **9 relevant studies missed**. Their own conclusion: the
results are "**insufficient to support the implementation of this approach in practical settings.**"
Two meta-analyses disagree about how good it is — Dai et al. (*JMIR* 2025) pool title/abstract
sensitivity at **0.73 (0.57–0.85)**; Xie et al. (*J Evid Based Med* 2026, 18 papers) pool **0.92
(0.81–0.96)** — and the spread is itself the finding.

And there is a methodological leak worth naming, because a QA product could easily reproduce it:
in both Oami (0.75 → 0.91) and Dai (0.73 → 0.98), the headline sensitivity is achieved **after
post-hoc prompt modification against the same gold set**. That is tuning on the oracle, not held-out
validation.

### 1.7 Citation and claim verification: the human baseline is not zero

Before judging LLM citation behaviour it is worth knowing what humans do. Baethge & Jergas (*Research
Integrity and Peer Review* 2025;10:13, 46 studies, ~32,000 quotations): "**16.9% (95% CI
14.1–20.0%) of quotations were incorrect, with approximately half classified as major errors (8.0%
[6.4–10.0%])**… Meta-regression showed **no significant improvement in quotation accuracy over
recent years** (slope −0.002, p = 0.85)." The 1985 original (de Lacey et al., *BMJ*
1985;291:884–6) found "The original author was misquoted in 15% of all references, and most of the
errors would have misled readers. Errors in citation of references occurred in 24%, of which 8% were
major errors."

**Roughly one in six human-written citations misrepresents its source, half of those materially, and
forty years of awareness has not moved it.** Any AI comparison needs that baseline, not zero.

Measured LLM rates, for contrast: Walters & Wilder (*Sci Rep* 2023;13:14045, 636 citations) —
"**55% of the GPT-3.5 citations but just 18% of the GPT-4 citations are fabricated**"; and of the
real ones, "43% of the real GPT-3.5 citations but just 24% of the real GPT-4 citations include
substantive citation errors." Chelli et al. (*JMIR* 2024;26:e53164, 471 references across 11
systematic reviews): hallucination rates **39.6% GPT-3.5, 28.6% GPT-4, 91.4% Bard**. And the
in-the-wild 2026 audit (Zhao et al., arXiv:2605.07723) checked **111 million references across 2.5
million papers** and estimates "**146,932 hallucinated citations in 2025 alone**", adding that
"**preprint moderation and journal publication processes capture only a fraction of these errors.**"

**Retraction checking** is the cheapest decidable check available and almost nobody runs it. The
Retraction Watch Database is now a Crossref public resource (acquired Sept 2023 for $175,000 plus
$120,000/year); the open CSV at `api.labs.crossref.org/data/retractionwatch` held **72,621 rows,
67,083 of them retractions**, when downloaded for this report. Zotero has checked libraries against
it automatically since 5.0.67 (June 2019). It matters because the literature does not
self-correct: Hsiao & Schneider (*QSS* 2022) found that of 13,252 post-retraction citation contexts,
"**only 722 (5.4%) acknowledged the retraction**", and Graña Possamai et al. (*JAMA Intern Med*
2025) found 61 systematic reviews in top journals that included retracted studies — recalculating
166 meta-analyses, "**the statistical significance of the results changed in 18 (11%)**", with
primary-outcome effect estimates moving ≥50% in 19% of cases.

The one attempt to fix this by telling people failed. **RetractoBot** (Bennett Institute, Oxford) ran
a randomised trial emailing ~246,749 authors who had cited retracted papers; the effect on citation
at 12 months was null (mean rate −0.007, 95% CI −0.055 to 0.041) — **even though 80.6% of the 15,667
respondents said they had not known.** Notification is not a control. A *blocking* check at the
point of writing is.

### 1.8 Living reviews: stopping is itself a documented act

Cochrane's living systematic review guidance (Dec 2019) is a rare written answer to "when is this
work finished?". A living review runs monthly searches, and every cycle produces a record even when
nothing changed — Scenario 1: "No new evidence … is identified" is logged as a dated "Amended"
event. Scenario 2 (new evidence, deferred) "should be justified and **requires editorial review and
approval**". Scenario 3 triggers a full MECIR update.

And retirement, §8.1:

> "**The reasons for an LSR to be transitioned out of living mode should be predetermined and
> specified in the protocol.**… the research question is no longer a priority for decision-making…
> **a reasonable level of certainty has been reached in the existing evidence**… research that might
> impact the conclusions of the review is no longer emerging… It is recommended that LSR teams
> assess the appropriateness of continuing to maintain the LSR **on an annual basis**."

§8.2 then requires the published review to state that it *was* living, over what period, and **why it
stopped**. COVID-NMA is the clean public example: "August 2023: The COVID-NMA living mapping and
synthesis has concluded and will no longer be updated."

Across all three kinds of stopping in this section — screening, rapid risk-of-bias assessment,
living-review retirement — the design is identical: **the stopping condition is written down before
the work starts, and the act of stopping is itself a recorded artefact with a justification
attached.**

---

## 2. Writing and reporting as a product

### 2.1 The stage model: four different edits, four different checkers

The publishing trades have the clearest existing answer to "what kind of QA does this need", because
they answer it four times per manuscript. Editors Canada's *Professional Editorial Standards 2024*
(editors.ca, [PDF](https://editors.ca/wp-content/uploads/2024/04/PES-2024-Interior_V7-Approved-Version-April-15-2024.pdf))
names them in one sentence each:

> **Structural editor:** Structural editing is assessing and shaping the overall organization and
> content of the material to optimize it for the intended audience, medium and purpose.
> **Stylistic editor:** Stylistic editing clarifies meaning, ensures coherence and flow at the
> paragraph and sentence level and refines the language.
> **Copy editor:** Copy editing corrects spelling, usage, grammar, and punctuation, and maintains
> consistency within the text.
> **Proofreader:** Proofreading checks all elements of the content and formatting for correctness,
> completeness and adherence to the style guide.

Two of its standards are directly transferable rules for staged QA:

- **A1.3** — "Be careful not to undo the work of the editors who came before you in the process and
  do not do the work of those who come later." **A3.3** — "Verify that all necessary edits have been
  applied at each preceding stage and that new problems have not been introduced."
- On error rates, and this is the honest statement a QA feature should copy: *"There is no standard
  for acceptable error rates in editing… No editor's work is perfect… Effective editorial processes
  function to minimize the percentage of errors missed in the process as a whole by having
  **multiple editorial reviews, preferably conducted by different editors (because editors are less
  likely to catch their own errors than those of other people)**. Each editor checks the work of
  previous editors while performing their own editorial functions."* And: *"Productivity rates are
  affected by so many variables that they are effectively meaningless."*

The Chicago Manual (17th ed., §2.48) draws the boundary that matters for sequencing: developmental
editing "may involve total rewriting or reorganization of a work, [so] it should be done—if
needed—**before** manuscript editing begins."

The oldest and sharpest specification of the distinction between a conformance check and a judgement
check is Van Buren & Buehler's *The Levels of Edit* (JPL/NASA CR-146584, 1976,
[PDF](https://ntrs.nasa.gov/api/citations/19760015018/downloads/19760015018.pdf)), which decomposes
editing into nine types and five cumulative levels. Its **Integrity Edit**:

> An Integrity Edit is concerned primarily with ensuring that the parts of a publication match. For
> example, if "Figure 1" is cited, an Integrity Edit will determine whether Figure 1 is included in
> the report. **However, it will not determine whether Figure 1 is actually the figure that is cited
> in the text.** Such a determination, which depends on the meaning of the text and the meaning of
> the figure, is included in Substantive Edit.

That is the conformance/correctness line, drawn in 1976, in one sentence, for one check. An AI
checker should own every Integrity Edit in a deliverable and none of the Substantive Edit.

The EFA's 2026 rate survey (1,100+ respondents) gives the throughput ladder that explains why:
proofreading runs at **5–20 pages/hour**, copyediting at 4.5–12, line editing 4–17, developmental
editing **3–13.5**. The deeper the judgement, the slower — which is exactly where automation earns
the most and is trusted the least.

### 2.2 Fact-checking: the artefact is an annotated proof, one tick per word

The *New Yorker* model is documented from inside by John McPhee ("Checkpoints", 9 Feb 2009,
[newyorker.com](https://www.newyorker.com/magazine/2009/02/09/checkpoints)), quoting checker Sara
Lippincott:

> **Each word in the piece that has even a shred of fact clinging to it is scrutinized, and, if
> passed, given the checker's imprimatur, which consists of a tiny pencil tick.**

The unit of verification is the **word**, and the artefact is a galley covered in ticks. Three
further conventions from the same piece are worth copying verbatim as states in a QA schema:

- **"on author"** — the disposition for a claim resting on the writer's own witness: "It was my
  experience, my description, my construction…"
- **TK** — a promissory placeholder the checker is expected to redeem: "`WHAT CITY, $000,000, name
  TK, number TK`… These are forms of promissory note, and a checker is expected to pay it."
- **Cut if unverifiable** — "If Wheeler's story was true, it would make it into print. **If
  unverifiable, it would be deleted.**"

McPhee prints a real before/after. The checker, Anne Stringfield, saw on her proof: *"Just above
Cromwell's Falls on Route 3 … is a Budweiser brewery that has a production average of **thirteen
thousand kegs a day**."* The number had been made up out of thin air; the real average was
**eighteen thousand**. A second: a sentence asserting "there are two Illinois Rivers in America",
checkable against Merriam-Webster's Geographical Dictionary — the checker found a **third**, in
Oregon.

**Der Spiegel's Dokumentation** is the industrialised version, and the Relotius affair produced an
unusually candid public description of it.
[Spiegel's own account (Dec 2018)](https://www.spiegel.de/kultur/gesellschaft/der-fall-claas-relotius-wie-das-spiegel-sicherungssystem-an-grenzen-stiess-a-1244593.html):
the relationship between desk and Dok is fixed in written "Grundsätze der Zusammenarbeit" with
separate "Verifikationsrichtlinien"; work begins when the text is finished; *"Als verifiziert gilt,
was mit zuverlässigen Quellen bestätigt ist"* — what counts as verified is what reliable sources
confirm. The worked examples are the same shape as McPhee's: a reporter describes roadside cacti in
Tanzania, the Dok asks whether cacti grow there (no); a reporter writes "16 February 2016, a foggy
cold day in Munich", the Dok checks the weather records.

The scale, and the artefact:

- **~80 staff, of whom ~50 are verifying documentarians**, organised into subject desks
  ([commission report PDF](https://cdn.prod.www.spiegel.de/media/67c2c416-0001-0014-0000-000000044564/media-44564.pdf)).
- A 2008 thesis counted **1,153 changes by the Dokumentation in a single issue**; net of spelling and
  style, **599 corrected errors and a further 400 inaccuracies**. Every one is talked through with
  the author, the checker then **verifies that the author actually made the correction**, "**und sie
  markiert, welche Fakten schlicht nicht verifiziert werden konnten**" — *and it marks which facts
  simply could not be verified.* That marking is the artefact a QA pass should emit.
- The tell that exposed the fraud was a **missing** annotation: the commission observed *"das
  Phänomen der »weißen«, also sehr leeren Manuskripte, an denen der Dokumentar wenig korrigiert
  hatte, was sehr unüblich ist"* — the phenomenon of "white", i.e. very empty manuscripts, with
  little corrected, which is highly unusual. **A checked manuscript is expected to come back dense.
  A clean check is itself a signal to investigate.**
- The known limit: *"Ihre Aufgabe ist die Textkontrolle, nicht die Personenkontrolle"* — the Dok
  checks the text, not the person. Exactly what a reporter uniquely witnessed cannot be checked,
  which is why Relotius survived.

The commission's remedies are the most directly copyable QA designs in this entire report, because
they are designs for *an organisation that already checks everything and still got fooled*:

1. **A weekly random extended verification.** Every Wednesday a random page number is drawn by app;
   whichever story is scheduled on that page is *erweitert verifiziert* — the author must hand over
   notes, photos, video, audio, phone numbers, e-mail addresses, and the contact details of any
   fixer, translator or driver. The stated purpose: *"Jemand wie Relotius wüsste, dass es nur eine
   Frage der Zeit ist, bis er entdeckt würde."* — someone like Relotius would know it was only a
   matter of time.
2. **A weekly random *un*checked text**, drawn the same way, *"der nicht gedokt wird. Jeder Autor
   muss also so arbeiten, als wäre er die letzte Instanz."* — so that every author must work as if
   they were the last line of defence. This is the countermeasure to the checker becoming a crutch.
3. **Annotated manuscripts as an entry condition.** "Auf den Manuskripten/Fahnen sind die Quellen zu
   markieren… **Texte, die diese Kriterien nicht erfüllen, werden in Zukunft nicht mehr gedokt.**" —
   sources must be marked on the galley and documents attached, sorted and marked; texts that do not
   meet this are *no longer checked at all*.
4. **Two independent sources** for any relevant factual assertion; research documented completely and
   **kept for at least two years**; awards entries require an extended verification first, and *"Eine
   Geschichte, die nicht erweitert verifiziert werden kann, darf nicht eingereicht werden."*
5. **Time budget as a named defect**: reporters get weeks; *"die Dokumentation aber für große Texte
   manchmal nur vier Stunden"* — the Dok sometimes gets four hours for a long piece.

The design a card-level QA feature should take from this: **sampling for deep verification**, with
the sample drawn publicly and unpredictably; **a deliberate no-check lane** so producers never treat
the checker as the guarantee; and **the annotated artefact as the price of admission** — the agent
must hand over its sources, marked, or the check does not run.

### 2.3 Reporting guidelines as checklists with a location column

The EQUATOR Network (equator-network.org, now listing **707 reporting guidelines**) is the closest
existing thing to a library of per-deliverable QA checklists. The reusable pattern is the column
layout: *item number · checklist item · **where in the manuscript it is reported***.

Real items, verbatim:

- **CONSORT 2010 item 6a** — "Completely defined pre-specified primary and secondary outcome
  measures, including how and when they were assessed." Columns: `Section/Topic | Item No |
  Checklist item | Reported on page No`.
- **PRISMA 2020 item 7** — "Present the full search strategies for all databases, registers and
  websites, including any filters and limits used." Column: `Location where item is reported`.
- **STROBE item 12(a)** — "Describe all statistical methods, including those used to control for
  confounding."
- **ARRIVE 2.0 item 2 (Sample size)** — "a. Specify the exact number of experimental units allocated
  to each group, and the total number in each experiment… b. Explain how the sample size was decided.
  Provide details of any *a priori* sample size calculation, if done."
- **ARRIVE 2.0 item 5 (Blinding)** — "Describe who was aware of the group allocation at the different
  stages of the experiment (during the allocation, the conduct of the experiment, the outcome
  assessment, and the data analysis)."
- **CARE** uses `Reported on Line` instead of a page.

**CONSORT 2025** (published simultaneously in five journals, 14 April 2025) is now a **30-item**
checklist; **SPIRIT 2025** is 34 items; **TRIPOD+AI** 27; **CHEERS 2022** 28.

How journals enforce it is the interesting part, because it is a spectrum:

- **ICMJE** only *encourages*: "Journals are encouraged to ask authors to follow these guidelines…
  Authors should look to see if the journal uses such checklists, and send them with the manuscript
  if they are requested."
- **PLOS ONE** requires the artefact and publishes it: "Provide … a completed CONSORT checklist as
  supporting information (which will be published alongside the paper, if accepted). **This should be
  named S1 CONSORT Checklist.**"
- **PLOS Medicine** goes furthest, and this is the version worth copying: **"Authors must complete
  the appropriate reporting checklist not only with page references, but also with sufficient text
  excerpted from the manuscript to explain how they accomplished all applicable items."** A checklist
  whose cells contain *quoted evidence*, not page numbers, is a checklist a machine can verify.
- **JAMA** uses it in review but does not publish it.
- **The BMJ** pushes it onto the referee: "Is the study fully reported in line with the appropriate
  reporting statement or checklist…? … Documents in the supplemental files, eg. checklists for
  reporting statements such as CONSORT, PRISMA, and STROBE… **Do these properly match what is in the
  manuscript? Do they contain information that should be better reported in the manuscript, or raise
  questions about the work?**"

### 2.4 Statistical reporting checkers: the purest example of a decidable check

**statcheck** ([CRAN](https://cran.r-project.org/web/packages/statcheck/index.html),
[statcheck.io](https://statcheck.io/)) is "a 'spellchecker' for statistics": it extracts APA-style
NHST results (t, F, r, χ², Z with df and p), **recomputes the p-value from the test statistic and
degrees of freedom**, and flags a mismatch — and separately flags a **decision error**, defined as an
inconsistency that "changes the statistical conclusion (assuming α = .05)."

Nuijten et al., *Behavior Research Methods* 2016;48:1205–1226: **30,717 articles** scanned, **16,695
(54.4%)** containing APA-style NHST, **258,105 p-values**. **49.6%** of those articles contained at
least one inconsistency; **12.9%** at least one gross inconsistency. "The prevalence of gross
inconsistencies was higher in p-values reported as significant than in p-values reported as
nonsignificant."

It is also the only checker in this report with published evidence that **deploying it changes
behaviour**. Nuijten & Wicherts (PsyArXiv 2023, doi:10.31234/osf.io/qejhk) compared two journals that
put statcheck into peer review (*Psychological Science*, *JESP*) with matched controls over **7,000+
articles and 147,000+ statistics**: "the decrease in both inconsistencies and decision
inconsistencies around p = .05 is considerably steeper in statcheck journals than in control
journals." *Psychological Science* now tells authors outright: "Authors who use null hypothesis
significance testing **should run their manuscript through StatCheck**."

**GRIM** (Brown & Heathers, *SPPS* 2017;8:363–369) is the same idea applied to means: for integer
data, the mean must be a multiple of 1/N (or 1/(N·k)), so a reported mean that is not reachable is
impossible. Of **260 articles**, only **71 were testable**; of those, **36 (~half) contained at least
one impossible mean** and **16 contained multiple**; of 21 datasets requested, 9 arrived, and **every
one confirmed at least one reporting error**. **GRIMMER** extends it to variances; **SPRITE**
reconstructs plausible raw samples from descriptives.

The lesson is the ratio: 260 → 71 testable → 36 flagged. **A decidable check is cheap, exhaustive
and catches a lot — but it only applies where it applies**, and knowing its domain of applicability
is part of the check.

### 2.5 Integrity screening: the similarity score is not a verdict

- **Text.** Crossref Similarity Check runs on iThenticate; Turnitin reports checking **10 million
  documents a year** against **244 million subscription sources** and **54 billion** crawled pages.
  The vendor documentation is unusually blunt about what the number means: "**The similarity score is
  simply the percentage of text in a submission that matches other sources**… It is perfectly natural
  for a submission to match against some of our database… **Even when a submission has quotation
  marks and references, the quoted text will show as a match**… Use this as a tool within your review
  process to make your own determination." And, in Turnitin's own bold: "**Turnitin does not check
  for plagiarism in writing.**" Exclusion filters (bibliography, quotes, small matches — default
  threshold **8 words**) change the index, which is the point: *the number is a function of the
  settings, so it can only ever be a triage signal.*
- **Images.** Bik, Casadevall & Fang (*mBio* 2016;7:e00809-16) screened **20,621 papers in 40
  journals, 1995–2014**: **3.8% contained problematic figures**, "with at least half exhibiting
  features suggestive of deliberate manipulation" — 29.4% simple duplication, 45.5% duplication with
  repositioning, 25.1% duplication with alteration. Between-journal range: **0.3% (J Cell Biol) to
  12.4% (Int J Oncol)**. The 2018 *MCB* follow-up found **6.1% of 960 papers** affected, estimated
  "**as many as 35,000 papers in the literature are candidates for retraction**", and measured the
  cost both ways: **~6 hours of staff time per problematic published paper** to resolve after the
  fact, versus **~30 minutes per problematic paper** when screening before publication. Prevention is
  an order of magnitude cheaper than correction — the strongest quantitative argument in this report
  for running the check *before* the artefact ships.
- **Deployed screening works and has a base rate.** AACR's Proofig pilot flagged **9.3% of 2,220**
  original research manuscripts at the accept stage; 63% of confirmed duplications were unintentional,
  28% deliberate-for-presentation, 2% unexplained (those manuscripts were withdrawn or rejected).
  Tool-assisted analysis took **4.4 minutes per manuscript versus 8 minutes manual**. ASM's
  ImageTwin pilot found duplications in **3.9%** of 2,627 accepted manuscripts and revoked acceptance
  for **six (0.23%)**. *Science* adopted Proofig across all six journals in 2024; Nature Portfolio
  "spot-checks" images from randomly chosen papers — sampling again.
- **Paper mills.** The STM Integrity Hub screens **200,000 papers a month** across 50+ publishers and
  **intercepts ~1,000 submissions a month** for suspected paper-mill activity, using **20 independent
  tools "each flagging different patterns of concern"** — a panel of narrow detectors, not one
  general judge. Cabanac's **Problematic Paper Screener** (dbrech.irit.fr) runs detectors named
  Tortured, SCIgen, Mathgen, Citejacked, Seek&Blastn, Feet of Clay, Annulled, Concerning, Suspect and
  Problematic Cell Lines. The "tortured phrases" detector (arXiv:2107.06751) is a beautiful example
  of an oracle built from a *known signature* rather than a known answer: "unexpected weird phrases in
  lieu of established ones, such as **'counterfeit consciousness' instead of 'artificial
  intelligence'**", or "**flag to clamor**" for "signal to noise", "profound neural organization" for
  "deep neural network", "credulous Bayes" for "naïve Bayes". Retractions passed **10,000 in 2023**
  for the first time (Nature, doi:10.1038/d41586-023-03974-8); the Crossref/Retraction Watch open
  data file downloaded today holds **72,621 rows, 67,083 of them retractions**.

### 2.6 What referees are actually asked, and the separation of axes

The most reusable thing in peer review is that the good forms **separate the axes that people
confuse**, and say which ones are out of scope.

- **PLOS ONE** publishes seven criteria, of which the operative ones are "Experiments, statistics,
  and other analyses are performed to a high technical standard and are described in sufficient
  detail" and "Conclusions are presented in an appropriate fashion and are supported by the data" —
  and then disclaims importance outright: "Unlike many journals which attempt to use the peer review
  process to determine whether or not an article reaches the level of 'importance' required by a
  given journal, **PLOS One uses peer review to determine whether a paper is technically rigorous**…"
- **eLife** splits the verdict into two named ladders, which is the single most copyable review
  vocabulary I found. *Significance:* **landmark → fundamental → important → valuable → useful**.
  *Strength of evidence:* **exceptional → compelling → convincing → solid → incomplete → inadequate**,
  where "solid" = "methods, data and analyses broadly support the claims with only minor weaknesses"
  and "incomplete" = "main claims are only partially supported". A card verdict that says
  *"important / solid"* carries more information than any single score.
- **F1000Research** uses a three-state gate — **Approved / Approved with Reservations / Not
  Approved** — and states the scope rule: "The rating should be based on whether the reported
  findings or analyses are correct and valid, **not on the novelty or importance** of an article."
- **Nature** asks nine named questions (Key results, Validity, Originality and significance, Data &
  methodology, Appropriate use of statistics and treatment of uncertainties, Conclusions, Suggested
  improvements, References, Clarity and context) with no numeric scale at all, and states the purpose
  plainly: "The primary purpose of the review is to provide the editors with the information needed
  to reach a decision."
- **NeurIPS 2025** asks for four 1–4 sub-ratings (Quality, Clarity, Significance, Originality) plus a
  1–6 overall, and — critically — a **confidence** score whose anchors describe *what the reviewer
  actually did*: "5: You are absolutely certain… You are very familiar with the related work and
  checked the math/other details carefully" down to "1: Your assessment is an educated guess… Math/
  other details were not carefully checked." It also asks reviewers to "state the clear criteria
  under which your evaluation score could increase or decrease", and warns that "superficial,
  uninformed reviews without evidence are worse than no review as they may contribute noise."
- **ACL Rolling Review** separates **Soundness** (1–5, about support for claims) from **Excitement**
  from the **venue recommendation** — three axes that a single "accept/reject" collapses.

### 2.7 Registered reports: moving the gate before the result exists

The structural move is to review the *protocol*, not the *finding*. The Center for Open Science:
"Registered Reports is a publishing format that emphasizes the importance of the research question
and the quality of methodology by **conducting peer review prior to data collection**… Manuscripts
that survive pre-study peer review receive an **in-principle acceptance that will not be revoked
based on the outcomes**." Over 300 journals now offer it.

PCI RR's Stage 1 criteria are the transferable checklist for "is this plan checkable?":

> 1C. The soundness and feasibility of the methodology and analysis pipeline… 1D. Whether the clarity
> and degree of methodological detail is sufficient to closely replicate the proposed study
> procedures and analysis pipeline **and to prevent undisclosed flexibility** in the procedures and
> analyses. 1E. Whether the authors have considered sufficient **outcome-neutral conditions** (e.g.,
> absence of floor or ceiling effects; **positive controls**; other quality checks)…

and the Stage 2 rule that prevents the result contaminating the review: "reviewers **do not
relitigate the theory, hypotheses or methods**, thus preventing knowledge of the results from
influencing recommendations." PCI RR also explicitly excludes importance from Stage 1: "subjective
judgments about the importance, novelty or timeliness of a research question are NOT a relevant
criterion."

The measured effect is large. Scheel, Schijen & Lakens (*AMPPS* 2021;4(2)) compared the first
hypothesis of each of **71 published Registered Reports** against **152 standard hypothesis-testing
articles**: "**96% positive results in standard reports but only 44% positive results in RRs**."
Whatever else that is, it is a measurement of how much a QA gate placed *before* the result changes
what gets reported.

---

## 3. Spreadsheet and financial-model assurance, and the audit profession's vocabulary

### 3.1 The error base rate, and why it is not surprising

Ray Panko's *What We Know About Spreadsheet Errors* (J. End User Computing 10(2), 1998, rev. 2008;
his university site is dead, retrievable via the Internet Archive) pooled thirteen field audits of
real operational spreadsheets:

> "Since 1995, when field audits began to use good (although usually not excellent) methodologies,
> **88% of the 113 spreadsheets audited in 7 studies** that reported the number of spreadsheets
> audited have contained errors… Furthermore, several of these studies only reported major errors."

and, on the per-cell rate: "In the four field audits for which we have cell error rates… **the CERs,
which range from 0.4% to 6.9%**, are similar in magnitude to CERs seen in experiments." The commonly
quoted "94%" is a different, more recent line: Powell, Baker & Lawson (2007b), 50 spreadsheets —
"Including poor practices, 1.8% of all formulas had issues. **Including poor practices, 94% of the
spreadsheets had issues.**"

The part of Panko worth carrying into any discussion of AI error rates is his framing of *why*:

> "Broadly speaking, when humans do simple mechanical tasks, such as typing, they make undetected
> errors in about **0.5%** of all actions. When they do more complex logical activities, such as
> writing programs, the error rate rises to about **5%**." … "Code inspection usually finds errors in
> about **5% of all program statements** after the developer has finished building and checking the
> module."

And his data on *self*-review is the sharpest maker-checker evidence outside Cochrane. In Galletta
et al.'s inspection experiments subjects found **51–56%** of seeded errors; in Panko (1999),
undergraduates inspecting **their own** spreadsheets found **16%**; team code inspection by the same
population found **83%**.

The canonical incidents (EuSpRIG's horror-stories register, verified here against primary documents)
are worth naming because each is a *type* of defect a checker can look for:

- **Copy-paste and a wrong denominator** — JPMorgan's 2013 Task Force Report on the London Whale:
  "**the model operated through a series of Excel spreadsheets, which had to be completed manually,
  by a process of copying and pasting data from one spreadsheet to another**"; and the error itself:
  "**after subtracting the old rate from the new rate, the spreadsheet divided by their sum instead
  of their average, as the modeler had intended. This error likely had the effect of muting
  volatility by a factor of two and of lowering the VaR.**" The governance failure is the more
  useful finding: the Model Review Group had already noted the process was "error prone" and "not
  easily scalable", and "**neither the Model Review Group nor CIO Risk followed up to determine
  whether the automation had in fact taken place.**" A separate one-day VaR error "was viewed as a
  one-off error, [so] it did not trigger further inquiry."
- **A range that stops five rows early** — Herndon, Ash & Pollin's critique of Reinhart & Rogoff
  (PERI WP322, 2013): "**RR averaged cells in lines 30 to 44 instead of lines 30 to 49**", excluding
  Australia, Austria, Belgium, Canada and Denmark. Worth stating precisely: the spreadsheet error
  alone accounts for −0.3pp; the full correction (2.2% vs −0.1%) also involves data exclusion and
  weighting choices.
- **A silently truncating file format** — Public Health England, October 2020, in PHE's own words:
  "**15,841 cases between 25 September and 2 October were not included** in the reported daily
  COVID-19 cases", because XLS caps at ~65,000 rows. O'Beirne's comment is the transferable rule:
  "anywhere data is exchanged between systems there must be checks and controls that **reconcile the
  output of a transformation stage to its input, such as record counts and hash totals**."
- **A row limit as a fraud signal** — JPMorgan noticed Charlie Javice's customer list "contained
  exactly **1,048,576 rows, the maximum permitted by Microsoft Excel**."
- **A sign error** — Fidelity Magellan, 1995: "the accountant omitted the minus sign on a net capital
  loss of $1.3 billion and incorrectly treated it as a net capital gain… the dividend estimate
  spreadsheet was off by $2.6 billion".
- **A mismatched key after a boundary change** — DfE's 2024-25 schools National Funding Formula,
  £370m (Wyman review, Dec 2023): "a coding mismatch resulted in the omission of pupil numbers from
  Cumberland and Westmorland & Furness, the two Local Authorities that replaced Cumbria… This
  understated forecast pupil numbers… **understated costs by 0.62% (£370 million)**." The review's
  conclusion is the single best statement in this report of why downstream re-performance does not
  save you: "**Checks later in the process, including tests like double-running the calculation,
  assumed these inputs were correct**… **Error trap checks at this stage would have avoided the
  error.** This means building into the code a number of calculation checks designed to flag if an
  error has taken place, such as confirming the total number of pupils to a known correct number."

### 3.2 The model-review issue log, from a real published template

The UK Department for Energy Security and Net Zero publishes its entire modelling QA apparatus,
including the live `.xlsm` template
([gov.uk](https://www.gov.uk/government/publications/energy-security-and-net-zero-modelling-quality-assurance-qa-tools-and-guidance)).
Its workbook tabs are: `User guide · Structure · Summary Page · Documentation · Structure & Clarity ·
Verification · Validation · Data & Assumptions · Model score · QA plan · QA history · Issues Log ·
Version Log · Lookups · Print`.

The **Issues Log header row, verbatim**:

```
Model Version | Unique Formula Reference | QA analyst | Worksheet | Cell/Range |
Comments | Priority | Impact | Notes | Date found (use ctrl+;) |
Date shared with model owner | Issue has been addressed?
```

with controlled vocabularies on a `Lookups` tab, verbatim:

```
Rating                 Weight      Priority  Weight    Importance     Weight
1) Excellent             0         High        9       Essential        3
2) Good                  1         Medium      3       Important        2
3) Some issues           3         Low         1       Best Practice    1
4) Needs improvement     7
5) Significant issues   10         Worst value 10
```

Two definitions from its Summary Page are worth keeping as the canonical split:

> "**Verification** is the process of checking that a model meets specifications and that it fulfils
> its intended purpose. Verification is used to check that **'the equations are being solved
> right'**." / "**Validation** is used to check that **'the right equations are being solved'**."

Its task register names the checks explicitly (Ve1 Formula correctness, Ve4 External links, Ve5
Autochecks & Regression Testing, Va2 backcasting, Va4 extreme values / model breaking, Va5
re-performance, DA2 data transformation). And it is proportionate by design — Table 2, "what QA
should be performed within a variety of time constraints", has a **0.5 days or less** row that ends:
"**Communicate that the model has not been fully QA'd in a clearance statement.**" A time-boxed check
that says so is an honest check.

### 3.3 Parallel rebuild beats cell-by-cell reading

The commercial model-audit firms agree on this and say it bluntly. Forvis Mazars (formerly Corality):

> "**Shadow modelling, not tick-and-bash.** We believe the standard market approach to reviewing a
> financial model is flawed. **Reading formulas cell by cell finds the errors you thought to look
> for. Rebuilding the model finds the ones you did not.** Our primary methodology is shadow
> modelling: using our own template model, we independently replicate the model's outputs from its
> inputs, end to end… **Where the two disagree, we have found something.** Cell-by-cell inspection
> then supplements the replication rather than substituting for it."

Operis uses the same word: "**parallel model reconstruction methodology**, supplemented by a review
of key cells, which is more robust than the more common 'cell-by-cell' analysis."

Three further features of that practice transfer directly to an agent QA design:

- **A scope statement that says what is *not* covered.** Forvis Mazars' "What we do not do" list:
  "Review the commercial merits, technical feasibility or reliability of the model's input data…
  Review input configurations other than the Base Case and the agreed Sensitivities… Provide any
  form of tax or accounting advice." Their gloss: "**A clear scope is not a narrow one. It is what
  makes the opinion worth relying on.**"
- **Independence as a structural rule:** "Independent model audit is deliberately kept separate from
  model development, modelling support and from advisory work on the same transaction." And the
  framing: "We treat model audit as an **assurance service rather than an advisory one**."
- **A red-flag report first.** "Typically within a week of receiving the model. A preliminary
  assessment covering common areas of significant error, which **surfaces show-stopper issues early
  enough to fix them without moving your dates.**" Operis grades its deliverables by liability:
  Formal Model Audit with an opinion letter (up to £20m), Due Diligence Review (up to £100,000),
  High Level Review (no formal liability). **The depth of the check and the weight of the claim are
  tied together explicitly.**

Government frameworks say the same thing more cheaply. The NAO's *Framework to review models* (2016)
lists "**independently recalculating the model**; for example, in a different software environment…
independently producing a **simplified version** of the selected methods and comparing with the
original model's results; and **directly testing the calculations**", plus Excel-specific smells:
"circular reference warnings; hard coding of values; linking of data from other files; and
complexity of formulae." HM Treasury's AQuA Book supplies the materiality principle: "Quality
assurance effort should be **appropriate to the risk** associated with the intended use of the
analysis and the complexity of the analytical approach."

ICAEW's *How to review a spreadsheet* adds the error category that no automated check can reach:
"**Meta errors: outside the scope of the spreadsheet's cells, such as when the right question is not
asked.**"

### 3.4 The standards: what a machine-checkable rule looks like

The **FAST Standard** (fast-standard.org, v02c July 2019, CC BY) is the most directly reusable,
because its ~90 numbered rules are written to be checkable. Verbatim examples:

- **FAST 3.02-01 Formulas must be consistent** — "Series calculations must be constructed from
  consistent formulas along the axis of presentation… **This is one of only a few universally
  accepted principles of good modelling.**"
- **FAST 2.01-03 Make only two columns matter** — "On the presumption that a series line item will be
  constructed via consistent formulas across the row, **the requirement for model review is limited
  to confirming only that the first cell in the range is logically sensible.**" *This is a rule about
  the model whose entire purpose is to make review tractable — the design pattern to copy.*
- **FAST 2.03-05** — "Include master error checks and alert indicators in the freeze pane."
- **FAST 3.03-01 Do not write a formula longer than your thumb** ("the rule of thumb") and
  **FAST 3.03-02 No formula should take more than 24 seconds to explain** ("the rule of seconds").
- **FAST 1.01-07 Calculate only once**; **FAST 1.01-11 Never release a model with purposeful use of
  circularity**; **FAST 3.04-01 Do not write formulas with embedded constants**; **FAST 3.06-02 Do
  not create daisy chains; do not link to links**; **FAST 4.01-02 Do not use the NPV function –
  ever**.

And the meta-rule about rules, which every QA checklist should adopt:

> "**Rules are meant to be broken. However, such pragmatic behaviour does not render the rule book
> useless. Breaking rules must be a conscious decision made with justification.**"

FAST also distinguishes prescriptive from suggestive language explicitly: "Rules are, for the most
part, prescriptive and use prescriptive language: **do not, always, never**. When a rule is
suggestive, less strong language is used: **avoid, should**."

**ICAEW's Twenty principles for good spreadsheet practice** (2024 edition) is the shorter, less
mechanical companion. Two principles carry the argument of this whole report:

> **17. Build in checks, controls, and alerts from the outset.** — "There are natural balances in the
> arithmetic of financial spreadsheets, such as total sales being the same whether sliced by region
> or product, or that **the trial balance nets to zero**… **Checks should be summarised sheet by
> sheet as well as at whole workbook level**, so that any errors arising are easily seen and can be
> quickly traced to the source."
> **18. Test and review to reduce the risk of error and identify inefficiencies.** — "**Self-review
> is limited in how many errors can be identified due to the bias of reviewing one's own work. To
> truly improve the chances of catching mistakes in a spreadsheet, a system of peer review is
> recommended.** Testing could include: adjusting inputs to see if the change in outputs matches
> expectation; testing extreme or impossible values… and double-checking the results of key
> calculations systematically."

The **SSRB** standards (v7.2) are now formally abandoned — the site's own note says "These Standards
are no longer actively maintained" — but its §11 is the most explicit checks specification in any of
these documents, and is worth lifting as a taxonomy: every check is classified as an **error check**,
a **sensitivity check** or an **alert check** (BPMS 11-1); each class gets its own summary
(11-5/11-6/11-7); there is at most one summary per class; and **BPMS 11-8**: "A message or indicator
that clearly notifies the model developer or user that a check has been triggered in a workbook
**should always be in view on every worksheet**."

The tooling that scans for these is mature and narrow. Operis's OAK produces a map that shows
"**consistency across rows and columns. Any inconsistencies or anomalies will be flagged**"; Rainbow
Analyst's Formula Scan map "**highlights blocks of copied formulas, so that omissions, hard-coded
numbers, and inconsistent formulas are immediately visible**"; Microsoft's own Inquire/Spreadsheet
Compare does cell-by-cell workbook diffs and a Workbook Analysis report with a Warnings section —
though, notably, Inquire is "available only in Excel for Windows in Microsoft 365 Apps for
enterprise plans", so the vendor's own consistency checker is not in most authors' hands.

### 3.5 Model risk management: independence, and "effective challenge"

The Federal Reserve / OCC *Supervisory Guidance on Model Risk Management* (SR 11-7 / OCC 2011-12,
4 April 2011) is the best short statement anywhere of why a checker has to be structurally separate,
and it names the three things that make challenge real:

> "A guiding principle for managing model risk is **'effective challenge' of models, that is,
> critical analysis by objective, informed parties who can identify model limitations and
> assumptions and produce appropriate changes. Effective challenge depends on a combination of
> incentives, competence, and influence.** **Incentives** to provide effective challenge to models
> are stronger when there is greater separation of that challenge from the model development process
> … **Competence** is a key to effectiveness since technical knowledge and modeling skills are
> necessary to conduct appropriate analysis and critique. Finally, **challenge may fail to be
> effective without the influence to ensure that actions are taken to address model issues.**"

Its definition of validation, and its independence clause:

> "**Model validation is the set of processes and activities intended to verify that models are
> performing as expected, in line with their design objectives and business uses.**"
> "**Validation involves a degree of independence from model development and use. Generally,
> validation should be done by people who are not responsible for development or use and do not have
> a stake in whether a model is determined to be valid.** Independence is not an end in itself but
> rather helps ensure that incentives are aligned… **it should be judged by actions and outcomes**…
> it is essential, however, that such validation work be subject to critical review by an independent
> party."

And its three core elements, which map cleanly onto the three kinds of check an agent QA pass could
run:

> "• **Evaluation of conceptual soundness, including developmental evidence** • **Ongoing
> monitoring, including process verification and benchmarking** • **Outcomes analysis, including
> back-testing**"

with back-testing defined as "**the comparison of actual outcomes with model forecasts during a
sample time period not used in model development**", and the warning that "**analysis of in-sample
fit and of model performance in holdout samples… are important parts of model development but are
not substitutes for back-testing**", plus "**any individual test will have weaknesses**". SR 11-7
also singles out exactly the artefact class this section is about: "**User-developed applications,
such as spreadsheets or ad hoc database applications used to generate quantitative estimates, are
particularly prone to model risk.**"

The hard-law version of four eyes is CRD IV Article 13(1): "The competent authorities shall grant
authorisation to commence the activity of a credit institution **only where at least two persons
effectively direct the business** of the applicant credit institution." (Note: the phrase "four-eyes
principle" does not appear verbatim in the EBA internal-governance guidelines; what those contain is
segregation of duties, ¶149: "**entrusting conflicting activities within the processing of
transactions… to different persons**".)

### 3.6 The audit profession's vocabulary, and why it is the right vocabulary

Financial audit has spent a century building a language for *checking someone else's work at a cost
proportionate to the stakes*. The pieces, in the order a QA design needs them (quotations from the
FRC's ISA (UK) editions, which reproduce the IAASB text with UK insertions):

**Assertions** (ISA 315, ¶A190) — the decomposition of "is this right?" into checkable claims. For
transactions: **occurrence, completeness, accuracy, cutoff, classification, presentation**. For
balances: **existence, rights and obligations, completeness, accuracy/valuation/allocation,
classification, presentation**. Each is a different question requiring different evidence; "existence"
and "completeness" in particular point in opposite directions (sample *from the ledger* to test
existence; sample *from the source documents* to test completeness). **Any QA schema for knowledge
work needs this move: break the verdict into named assertions that each have their own evidence.**

**Materiality** (ISA 320) — "Misstatements… are considered to be material if they, individually or in
the aggregate, **could reasonably be expected to influence the economic decisions of users**"; and
users are assumed to "**understand that financial statements are prepared, presented and audited to
levels of materiality**". **Performance materiality** is set lower than overall materiality to absorb
**aggregation risk** — "the probability that the aggregate of uncorrected and undetected
misstatements exceeds materiality". And the floor, ISA 450 ¶A2, which is the most quotable sentence
in the set:

> "**'Clearly trivial' is not another expression for 'not material.' Misstatements that are clearly
> trivial will be of a wholly different (smaller) order of magnitude, or of a wholly different
> nature than those that would be determined to be material… When there is any uncertainty about
> whether one or more items are clearly trivial, the misstatement is considered not to be clearly
> trivial.**"

**Sampling** (ISA 530) — "The application of audit procedures to less than 100% of items within a
population of audit relevance such that **all sampling units have a chance of selection**". The
vocabulary worth borrowing wholesale: **tolerable misstatement** and **tolerable rate of deviation**
(thresholds set in advance), **sampling risk** vs **non-sampling risk**, **stratification**, and —
critically — **anomaly**, "a misstatement or deviation that is **demonstrably not representative**",
which "may be excluded when projecting misstatements to the population" but whose own effect "still
needs to be considered". Plus the requirement that makes sampling meaningful at all (¶14): "**For
tests of details, the auditor shall project misstatements found in the sample to the population.**"
A sample that is not projected is anecdote.

**The seven procedures** (ISA 500, ¶A18–A29) — **inspection, observation, external confirmation,
recalculation, reperformance, analytical procedures, inquiry**. Two are exactly what an AI checker
does best, and their definitions are one line each:

> "**Recalculation consists of checking the mathematical accuracy of documents or records.
> Recalculation may be performed manually or electronically.**"
> "**Reperformance involves the auditor's independent execution of procedures or controls that were
> originally performed as part of the entity's internal control.**"

**Analytical procedures** (ISA 520) — the check that is closest to "does this look right?", made
rigorous by requiring the expectation to be set *first*. ¶5 requires the auditor to "**develop an
expectation of recorded amounts or ratios and evaluate whether the expectation is sufficiently
precise to identify a misstatement**" and to "**determine the amount of any difference of recorded
amounts from expected values that is acceptable without further investigation**". ¶A16: "**as the
assessed risk increases, the amount of difference considered acceptable without investigation
decreases.**" *Expectation first, threshold first, then look.* That is a far better design for an AI
sanity check than asking a model whether a number seems plausible after seeing it.

**Documentation** (ISA 230 / PCAOB AS 1215) — the standard for a working paper is a *reader test*:

> "The auditor shall prepare audit documentation that is sufficient **to enable an experienced
> auditor, having no previous connection with the audit, to understand:** (a) The nature, timing and
> extent of the audit procedures performed…; (b) The results… and the audit evidence obtained; and
> (c) Significant matters arising… the conclusions reached thereon, and significant professional
> judgments made." (ISA 230 ¶8)
> "…the auditor shall record: (a) The identifying characteristics of the specific items or matters
> tested; **(b) Who performed the audit work and the date such work was completed; and (c) Who
> reviewed the audit work performed and the date and extent of such review.**" (¶9)

AS 1215 adds an append-only rule that is exactly a QA audit trail: documentation must be archived
within **14 days** of report release (reduced from 45 by PCAOB Release 2024-004), and after that
"**Audit documentation must not be deleted or discarded… however, information may be added. Any
documentation added must indicate the date the information was added, the name of the person who
prepared the additional documentation, and the reason for adding it.**"

**Engagement quality review** (PCAOB AS 1220) is the four-eyes gate at the top, and its objective
sentence is the best available description of what a *second reviewer of judgement* is for:

> "The objective of the engagement quality reviewer is **to perform an evaluation of the significant
> judgments made by the engagement team and the related conclusions reached**… in order to determine
> whether to provide concurring approval of issuance."

with independence rules that are structural, not attitudinal — the reviewer "**should not make
decisions on behalf of the engagement team**" (¶.07), may not have been engagement partner on
"**either of the two audits preceding**" (¶.08), and must possess "**the level of knowledge and
competence… required to serve as the engagement partner**" (¶.05) — and a gate that is binary and
blocking:

> "…**may provide concurring approval of issuance only if… he or she is not aware of a significant
> engagement deficiency**" — defined as the team "failed to obtain sufficient appropriate evidence",
> "reached an inappropriate overall conclusion", the report "is not appropriate", or the firm "is
> not independent". And: "**the firm may grant permission to the client to use the engagement report
> only after the engagement quality reviewer provides concurring approval of issuance.**"

**Tick marks** — the notation that makes a checked document readable at a glance. The rule, verbatim
from the IRS *Internal Revenue Manual* 4.10.9.8.2 (04-22-2024):

> "**Tick marks are used to simplify documentation of conditions found and work performed. Tick marks
> do not need to be standardized throughout the case file, but they must be consistent throughout the
> workpapers for an issue. Tick mark explanations must be a part of the workpaper or included in a
> separate tick mark legend workpaper.**"

Consistency within a unit of work, plus a legend attached to the artefact — not a global standard.
(A common secondary caveat: the frequently-quoted classic US legend "^ = footed, ✓ = agreed to
supporting documentation, TB = agreed to trial balance" could **not** be traced to a primary source;
the verifiable legends are simpler, and are quoted in section (b).)

---

## 4. Law, policy, consulting and analysis deliverables

This is the area where the QA problem is hardest — the deliverable is an argument, not a number — and
also the area where the last three years have produced the largest natural experiment in what happens
when an AI does the work and nobody checks it.

### 4.1 Cite-checking: the one decidable layer of a legal argument

A law-review cite check splits the job explicitly. The *Virginia Law Review* Slatebook's Part I is
"**SUBSTANTIATION**", Part II "FORMATTING TEXT & CITATIONS" — and the journal's editorial philosophy
page calls both **non-discretionary**: "Non-discretionary changes are those that must be made, and
include things like spelling, grammar, substantiation, and Bluebook formatting."

What substantiation actually means, from Stanford Law's library guide:

> "Cite checking is primarily about making sure **the sources cited in the footnotes support the
> author's associated claims**… accurate (the citation content and format leads the reader to the
> correct source), **valid (the source actually supports the author's claim/statement)**,
> non-plagiarized (every proposition is properly credited)… Is a citation needed? / Does the cited
> authority support the statement made in the text? / Are the introductory signals and parentheticals
> appropriate…? / Does the citation include all necessary content (including pincites) and is it
> formatted properly?"

The artefact is a **source-and-cite packet**, which UCLA's guide describes as a spreadsheet: "an
entry for each source in each footnote… column headings… typically include: Footnote Number, Author,
Title, Source, Date, Call Number/URL, Current Location". And because Bluebook Rule 18.2 requires
printed or authenticated sources, a cite checker must physically *pull* each one — an exact page
image, not a database paraphrase.

Three properties of this make it the best available model for AI-produced claims:

1. **The signal is a formal assertion about the strength of support.** Per Cornell LII's restatement:
   no signal means "the citation directly states the proposition"; **See** means it "supports the
   proposition… implicitly or in the form of dicta"; **Cf.** "supports by analogy"; **But see**
   "clearly supports the contrary". As the CC0 Indigo Book puts it: "A signal illustrates the
   relationship between the author's assertion and the source cited for that assertion." *The writer
   must grade their own evidence, in a controlled vocabulary, in front of the reader.* This is
   precisely what ALCE's citation recall measures, and it existed a century earlier.
2. **Negative treatment is a separate, machine-maintained axis.** KeyCite's vocabulary, verbatim from
   Thomson Reuters: a **Red Flag** "warns that the case or administrative decision is **no longer good
   law** for at least one of the points of law it contains"; a **Yellow Flag** "warns that the case…
   has **some negative history**, but hasn't been reversed or overruled"; a **Blue Striped Flag**
   indicates a pending appeal; **Blue H** history exists; **Green C** citing references but no
   negative history. Westlaw Edge adds an **orange** "KeyCite Overruling Risk" icon that "leverages
   artificial intelligence to mark cases… that have **no direct citations pointing to their
   invalidity**" — i.e. implicitly overruled. Shepard's uses a red stop sign (Warning), **orange box
   with Q** (Questioned), yellow triangle (Caution), **green diamond** (Positive), blue A/I
   (analysis / citation information available). This is a maintained retraction index for law, of the
   kind the sciences only got in 2023.
3. **Depth of engagement is graded too.** KeyCite: **Examined** ("extended discussion… usually more
   than a printed page"), **Discussed** ("more than a paragraph but less than a printed page"),
   **Cited** ("usually less than a paragraph"), **Mentioned** ("a brief reference… usually in a
   string citation"). A citation count that does not distinguish these is not a measurement.

There is even a shipped machine checker for the narrowest decidable part: Westlaw's **QuoteRight**,
which "compares the quotation in the user's document against the actual quotation."

### 4.2 The natural experiment: what happened when nobody checked

*Mata v. Avianca* (S.D.N.Y., 22 June 2023, Judge Castel) is the origin, and the opinion states the
principle a QA feature exists to serve:

> "Technological advances are commonplace and there is nothing inherently improper about using a
> reliable artificial intelligence tool for assistance. **But existing rules impose a gatekeeping role
> on attorneys to ensure the accuracy of their filings.** … [Counsel] abandoned their
> responsibilities when they submitted non-existent judicial opinions with fake quotes and citations
> created by the artificial intelligence tool ChatGPT, **then continued to stand by the fake opinions
> after judicial orders called their existence into question.**"

Six cases were fabricated; the sanction was $5,000; and of the fake *Varghese*, the court said simply:
"**Its legal analysis is gibberish.**" The harms list is worth keeping because it generalises to any
knowledge-work deliverable: "The opposing party wastes time and money in exposing the deception. The
Court's time is taken from other important endeavors. **The client may be deprived of arguments based
on authentic judicial precedents.**"

What followed is a scaling curve. Damien Charlotin's **AI Hallucination Cases** database
(damiencharlotin.com/hallucinations, CC BY, read 21 Sept 2026) holds **2,046 decisions**: **16 in
2023, 61 in 2024, 851 in 2025, 1,118 in 2026 so far.** By actor: **pro se litigants 1,175, lawyers
815, judges 32.** Professional sanctions appear in only about 8% of rows.

The judicial reasoning is where the QA lessons are:

- **Negligence, not bad faith, is the usual finding.** *United States v. Cohen* (S.D.N.Y. 2024): "There
  was only one problem: **The cases do not exist.** … His citation to non-existent cases is
  embarrassing and certainly negligent, perhaps even grossly negligent. But the Court cannot find
  that it was done in bad faith." Cohen "did not realize" Google Bard "was a generative text service".
- **The failure is at signing, not at drafting.** *Lnu v. Blanche* (9th Cir., June 2026, published)
  held that "**the rules are not violated at the point of research and drafting, but at the point of
  signing and filing**". *Noland v. Land of the Free* (Cal. Ct. App., Sept 2026, $10,000, certified
  for publication) states the rule as broadly as it can be stated: "**Simply stated, no brief,
  pleading, motion, or any other paper filed in any court should contain any citations—whether
  provided by generative AI or any other source—that the attorney responsible for submitting the
  pleading has not personally read and verified.**" (Its opening brief contained 23 case quotations,
  **21 of which were fabrications**.)
- **The checker downstream is the one who pays.** *Lacey v. State Farm* (C.D. Cal., May 2025,
  **$31,100**), Special Master Wilner: "**Directly put, Plaintiff's use of AI affirmatively misled
  me. I read their brief, was persuaded (or at least intrigued) by the authorities that they cited,
  and looked up the decisions to learn more about them — only to find that they didn't exist. That's
  scary. It almost led to the scarier outcome (from my perspective) of including those bogus
  materials in a judicial order.**"
- **The measured deterrent is failing.** *Johnson v. Dunn* (N.D. Ala., July 2025): "Fabricating legal
  authority is serious misconduct that demands a serious sanction… **it demands substantially greater
  accountability than the reprimands and modest fines that have become common**… As a practical
  matter, time is telling us – quickly and loudly – that those sanctions are insufficient deterrents."

And the *Coomer v. Lindell* hearing transcript contains the whole problem in two lines:

> "THE COURT: And did you double-check any of these citations once it was run through artificial
> intelligence? MR. KACHOUROFF: **Your Honor, I personally did not check it.**"

### 4.3 The standing orders, and what they turned into

Judge Brantley Starr's May 2023 "Mandatory Certification Regarding Generative Artificial
Intelligence" (N.D. Tex.) is the canonical text:

> "All attorneys and pro se litigants appearing before the Court must, together with their notice of
> appearance, file on the docket a certificate attesting either that **no portion of any filing will
> be drafted by generative artificial intelligence** … **or that any language drafted by generative
> artificial intelligence will be checked for accuracy, using print reporters or traditional legal
> databases, by a human being.** … These platforms in their current states are prone to hallucinations
> and bias. On hallucinations, they make stuff up—even quotes and citations. … **As such, these
> systems hold no allegiance to any client, the rule of law, or the laws and Constitution of the
> United States (or, as addressed above, the truth). Unbound by any sense of duty, honor, or justice,
> such programs act according to computer code rather than conviction, based on programming rather
> than principle.**"

The certification form's scope is the part worth copying: verification covers "**quotations,
citations, paraphrased assertions, and legal analysis**" — four categories, not just citations.

**That order has since been rescinded**, and what replaced it is the structural story. N.D. Tex.
Local Rule 7.2(f) now says: "A brief prepared using generative artificial intelligence must disclose
this fact on the first page under the heading 'Use of Generative Artificial Intelligence.'… A party
who files a brief that does not contain the disclosure … **certifies that no part of the brief was
prepared using generative artificial intelligence.**" **Certification became disclosure, and silence
became the certification.** Judge Baylson's E.D. Pa. standing order (6 June 2023) remains live and
pairs the two: disclose use, and "**CERTIFY, that each and every citation to the law or the record in
the paper, has been verified as accurate.**"

Where it landed: the Fifth Circuit proposed a blanket certification rule (proposed 5th Cir. R. 32.3)
and then dropped it; the Duke/RAILS tracker's own conclusion is that "**Few of these policies govern
behaviors that are not already addressed by existing rules of professional responsibility**", with "an
ongoing shift from judge-level orders… towards more comprehensive and widely applicable AI governance
in the courts." Proposed **FRE 707** (machine-generated evidence held to Rule 702's standards absent
an expert) was published for comment in August 2025; as of the May 2026 Advisory Committee report,
"**The Committee does not recommend action on the proposed Rule 707 at this time.**" The Judicial
Conference's September 2026 statement is the operative norm: "**courts have been cautioned not to
delegate core judicial functions to AI, including decision-making or case adjudication. And all
Judiciary users have been reminded that they are accountable for all work performed with the
assistance of AI.**"

### 4.4 The measurement: RAG does not eliminate hallucination

Magesh, Surani, Dahl, Suzgun, Manning & Ho, *Hallucination-Free? Assessing the Reliability of
Leading AI Legal Research Tools* (arXiv:2405.20362; *J. Empirical Legal Studies* 22(2):216–242, 2025)
is the study that matters, because it tests the exact architecture most AI QA proposals assume:

> "Recently, certain legal research providers have touted methods such as retrieval-augmented
> generation (RAG) as '**eliminating**' … or '**avoid[ing]**' hallucinations, or guaranteeing
> '**hallucination-free**' legal citations… **We demonstrate that the providers' claims are
> overstated.** While hallucinations are reduced relative to general-purpose chatbots (GPT-4), we
> find that the AI research tools made by LexisNexis (Lexis+ AI) and Thomson Reuters (Westlaw
> AI-Assisted Research and Ask Practical Law AI) each **hallucinate between 17% and 33% of the
> time**."

| System | Accurate | Incomplete | **Hallucinated** |
|---|---|---|---|
| Lexis+ AI | 65% | 18% | **17%** |
| Westlaw AI-Assisted Research | 42% | 25% | **33%** |
| Ask Practical Law AI | 20% | 63% | **17%** |
| GPT-4 (general purpose) | 49% | 8% | **43%** |

Two definitions from the paper deserve to be adopted directly, because they are the right ones for
any claim-checking design:

> "A response is **misgrounded** if key factual propositions are cited but **misinterpret the source
> or reference an inapplicable source**." / "A response is considered **hallucinated** if it is either
> **incorrect or misgrounded**."

That is, *a real citation attached to a claim it does not support is a hallucination*. The authors'
reductio explains why the narrow vendor definition is useless: "Failing to capture that dimension of
hallucination would require us to conclude that a tool that links only to *Brown v. Board of
Education* on every query… has provided 'hallucination-free' citations, a plainly irrational result."

And the finding with the sharpest product implication:

> "With longer answers, Westlaw contains more falsifiable propositions and therefore has a greater
> chance of containing at least one hallucination. **Lengthier answers also require substantially
> more time to check, verify, and validate, as every proposition and citation has to be independently
> evaluated.**"

**Verbosity is not free: it multiplies both the error rate and the cost of checking.** Any QA design
that rewards thoroughness by length is making its own job harder twice over.

### 4.5 Structured analytic techniques: QA for an argument

Where the deliverable is a judgement, the intelligence community's answer is not a checklist of facts
but a **procedure that forces the analyst to attack their own conclusion**.

**Analysis of Competing Hypotheses** (Heuer, *Psychology of Intelligence Analysis*, CIA CSI, 1999,
ch. 8) is eight steps, verbatim:

> 1. Identify the possible hypotheses to be considered. Use a group of analysts with different
>    perspectives to brainstorm the possibilities.
> 2. Make a list of significant evidence and arguments for and against each hypothesis.
> 3. Prepare a matrix with hypotheses across the top and evidence down the side. Analyze the
>    '**diagnosticity**' of the evidence and arguments—that is, identify which items are most helpful
>    in judging the relative likelihood of the hypotheses.
> 4. Refine the matrix. Reconsider the hypotheses and delete evidence and arguments that have no
>    diagnostic value.
> 5. Draw tentative conclusions about the relative likelihood of each hypothesis. **Proceed by trying
>    to disprove the hypotheses rather than prove them.**
> 6. Analyze how sensitive your conclusion is to a few critical items of evidence…
> 7. Report conclusions. **Discuss the relative likelihood of all the hypotheses, not just the most
>    likely one.**
> 8. Identify **milestones for future observation** that may indicate events are taking a different
>    course than expected.

Heuer's justification is the strongest argument in this report for evidence-first, conclusion-last QA:

> "**No matter how much information is consistent with a given hypothesis, one cannot prove that
> hypothesis is true, because the same information may also be consistent with one or more other
> hypotheses. On the other hand, a single item of evidence that is inconsistent with a hypothesis may
> be sufficient grounds for rejecting that hypothesis.**"
> "In examining the matrix, look at the **minuses**… The hypotheses with the fewest minuses is
> probably the most likely one… **What is difficult to find, and is most significant when found, is
> hard evidence that is clearly inconsistent with a reasonable hypothesis.**"

Two more of his lines are directly reusable as QA prompts. The mechanical one: "In Step 3, you work
**across the rows**… In Step 5, you work **down the columns**." And the absence test, which is exactly
the check an agent's "I found no evidence of X" needs:

> "**Whenever an intelligence analyst is tempted to write the phrase 'there is no evidence that . .
> .,' the analyst should ask this question: If this hypothesis is true, can I realistically expect to
> see evidence of it?**"

Finally, the honest limit, which every QA feature should print somewhere: "There is no guarantee that
ACH or any other procedure will produce a correct answer… Analysis of competing hypotheses does,
however, **guarantee an appropriate process of analysis**." *Process assurance, not answer assurance.*

The CIA's *Tradecraft Primer* (2009) organises the rest into **Diagnostic** (Key Assumptions Check,
Quality of Information Check, Indicators or Signposts of Change, ACH), **Contrarian** (Devil's
Advocacy, Team A/Team B, High-Impact/Low-Probability, "What If?"), and **Imaginative Thinking**
(Brainstorming, Outside-In, Red Team, Alternative Futures). The **Key Assumptions Check** is three
steps — write down the current analytic line; "articulate all the premises, both stated and unstated…
which are accepted as true for this analytic line to be valid"; "challenge each assumption, asking
why it 'must' be true and whether it remains valid under all conditions" — with the closing question
"**If the assumption proves to be wrong, would it significantly alter the analytic line? How?**" and
the framing that keeps it from being destructive: "**The goal is not to undermine or abandon key
assumptions; rather, it is to make them explicit and identify what information or developments would
demand rethinking them.**"

The Primer also supplies a labelling rule that any adversarial AI check should adopt: a devil's
advocacy product must be "**clearly lay[ing] out the conventional wisdom and … identified as an
explicitly 'Devil's Advocate' project; otherwise, the reader can become confused as to the current
official view**."

### 4.6 ICD 203: the only published standard for "how to write a judgement"

Intelligence Community Directive 203, *Analytic Standards* (DNI Clapper, 2 January 2015) is the
closest thing in existence to a reporting guideline for analytical prose. Five standards — objective;
independent of political consideration; timely; based on all available sources — and nine **Analytic
Tradecraft Standards**, of which five are directly implementable as automated checks on an agent's
output:

> **(1) Properly describes quality and credibility of underlying sources, data, and methodologies** —
> "**Source summary statements**… are strongly encouraged and should be used to provide a holistic
> assessment of the strengths or weaknesses in the source base and explain which sources are most
> important to key analytic judgments."
> **(2) Properly expresses and explains uncertainties associated with major analytic judgments** —
> "specifically **the likelihood of occurrence of an event or development, and the analyst's
> confidence in the basis for this judgment**… As appropriate, products should identify **indicators
> that would alter the levels of uncertainty**."
> **(3) Properly distinguishes between underlying intelligence information and analysts' assumptions
> and judgments** — "**Assumptions are defined as suppositions used to frame or support an
> argument**… **Judgments are defined as conclusions based on underlying intelligence information,
> analysis, and assumptions.** Products should state assumptions explicitly when they serve as the
> linchpin of an argument or when they bridge key information gaps. **Products should explain the
> implications for judgments if assumptions prove to be incorrect.**"
> **(4) Incorporates analysis of alternatives** — "the systematic evaluation of differing hypotheses…
> Analytic products should identify and assess plausible alternative hypotheses."
> **(6) Uses clear and logical argumentation** — "Products should be **internally consistent and
> acknowledge significant supporting and contrary information affecting judgments**."
> **(7) Explains change to or consistency of analytic judgments** — "**should state how their major
> judgments on a topic are consistent with or represent a change from those in previously published
> analysis**… should avoid using boilerplate language."
> **(8) Makes accurate judgments and assessments** — "**should not avoid difficult judgments in order
> to minimize the risk of being wrong.**"

The probabilistic-language table is a controlled vocabulary with numeric bands, mandatory ("an
analytic product **must** use one of the following sets of terms"):

| almost no chance | very unlikely | unlikely | roughly even chance | likely | very likely | almost certain(ly) |
|---|---|---|---|---|---|---|
| remote | highly improbable | improbable | roughly even odds | probable | highly probable | nearly certain |
| **01–05%** | **05–20%** | **20–45%** | **45–55%** | **55–80%** | **80–95%** | **95–99%** |

> "Analysts are strongly encouraged not to mix terms from different rows. Products that do mix terms
> must include a disclaimer clearly noting the terms indicate the same assessment of probability."

And the separation rule, which is the single most transferable sentence in the directive:

> "To avoid confusion, products that express an analyst's confidence in an assessment or judgment
> using a 'confidence level' (e.g., 'high confidence') **must not combine a confidence level and a
> degree of likelihood, which refers to an event or development, in the same sentence**."

**Likelihood is about the world; confidence is about your basis.** ICA 2017-01D's front matter spells
it out — "**High confidence** generally indicates that judgments are based on high-quality
information from multiple sources. **High confidence in a judgment does not imply that the assessment
is a fact or a certainty; such judgments might be wrong.**" — and its key judgments show the format
carrying a recorded disagreement: "All three agencies agree with this judgment. **CIA and FBI have
high confidence in this judgment; NSA has moderate confidence.**"

### 4.7 Red teams, murder boards, pre-mortems — and how they fail

**Red teaming** is defined by the US Army's UFMCS handbook as "a flexible cognitive approach to
thinking and planning… It uses structured tools and techniques to help us ask better questions,
challenge explicit and implicit assumptions, expose information we might otherwise have missed, and
develop alternatives we might not have realized exist" — "in essence, **a form of risk management for
the human brain**." Joint doctrine (JP 2-0) adds the independence requirement: "an **independent
capability** to fully explore alternatives in plans and operations… from the perspective of
adversaries and others."

Its preconditions are stated bluntly: "Red Teams require **top cover** to be allowed to challenge the
conventional wisdom… **Constituting a Red Team with those the organization 'can afford to give up' is
a sure recipe for failure.**"

**The murder board** is the staged adversarial rehearsal. The NSA's own history describes the JCS
convening "a group of personnel who help the nominee prepare for difficult oral testimony", and
quotes Gen. Nakasone: "**The white boards are much more difficult than anything I faced in my
confirmation**… it was just… **it was a blood-letting.**" A 1986 CIA memorandum sets out the design:
"The purpose of the 'murder board' is to cover in advance questions that are likely to be asked
during the confirmation hearing" — with a distribution list that assigns each principal a hostile
topic (oversight, independence of view, covert action, counterintelligence, politicized
intelligence). The Center for Presidential Transition's rule of thumb: "**Hold at least two murder
board sessions** to repeatedly test the nominee's preparation and delivery, preceded by informal,
low-pressure sessions."

**And the best-documented failure of a challenge function is worth more than all of the above.** The
Senate Intelligence Committee's 1978 review of the Team A/Team B exercise found:

> "The composition of the B Team dealing with Soviet objectives was so structured that **the outcome
> of the exercise was predetermined**… They needlessly allowed **analytic mismatches**, by sending
> relatively junior specialists into the debating arena against prestigious and articulate B Team
> authorities."

Two design rules fall out, and both apply directly to an AI checker: **a challenge function staffed
from one school of opinion produces a foregone conclusion**, and **mismatching the seniority (read:
capability) of challenger and challenged destroys the exercise.** A weaker model reviewing a stronger
one is an analytic mismatch.

The WMD Commission reached the same conclusion about institutionalising dissent, and its caveat is
the argument against a permanent "QA agent":

> "**An office solely responsible for dissenting opinions is at risk of losing credibility over
> time**… we are afraid that an office dedicated to independent analysis would—in the long run—end up
> having its own biases… We thus recommend that the DNI give particular 'red-team' or 'devil's
> advocate' assignments to individuals or offices **on a case-by-case basis**, rather than trying to
> produce all alternative analysis through a separate office… **such independent analysis must become
> a habitual analytic practice for all analysts.**"

**The pre-mortem** (Gary Klein, *HBR*, September 2007) is the cheapest of these and the easiest to
automate a prompt for:

> "**Unlike a typical critiquing session, in which project team members are asked what *might* go
> wrong, the premortem operates on the assumption that the 'patient' has died, and so asks what *did*
> go wrong.** … The leader starts the exercise by informing everyone that **the project has failed
> spectacularly**. Over the next few minutes those in the room **independently** write down every
> reason they can think of for the failure—**especially the kinds of things they ordinarily wouldn't
> mention as potential problems, for fear of being impolitic**… Next the leader asks each team
> member… to read one reason from his or her list; everyone states a different reason until all have
> been recorded."

Klein's claimed effect: prospective hindsight "**increases the ability to correctly identify reasons
for future outcomes by 30%**" (attributed to Mitchell, Russo & Pennington 1989). Note the mechanism
he names — it is a *psychological safety* device as much as a cognitive one: "By making it safe for
dissenters who are knowledgeable about the undertaking and worried about its weaknesses to speak up."
And note the sequencing: **write independently first, then pool** — the same rule as Cochrane's dual
screening and DOE's independent verification.

### 4.8 Consulting's review gate is unpublished; audit's is a public standard

Worth stating as a finding, because it is checkable: **the audit profession publishes its review gate
and strategy consulting does not.** BCG and Bain both assert a Code of Conduct and (Bain)
"Professional Standards" and publish neither; neither describes any engagement-level review gate.

What consulting method is publicly documented comes from ex-partners, not firms. Conn & McLean's
seven steps — *define the problem → disaggregate → prioritize → workplan → analyze → synthesize →
communicate*, with **iterate** in the hub — and the **day-1 answer**: "**What's the one-day answer?**
This means we ask our team to have a coherent summary of our best understanding of the problem and a
solution path **at any point in the project, not just at the end.**" Their framing of hypothesis work
is explicitly disconfirmatory: "He hadn't reached a conclusion by framing it this way… **He was using
the hypothesis to bring forth the arguments to either disprove it or support it.**" And, for solo
work: "we suggest **building in review processes** that you can use with family and colleagues to get
the higher objectivity and other bias-fighting benefits of a team."

Barbara Minto's own site supplies the nearest published "so what?" test: "to make sure that when you
say 'There are three reasons for making this change,' there are in fact only three reasons, that you
have identified the right three, and that **you have drawn the appropriate insight from having
identified them. ('There are three reasons' is not an appropriate insight!)**"

The one place consulting has a published QA gate is where a regulator imposed one. McKinsey's
December 2024 HHS-OIG Corporate Integrity Agreement requires "a **client and engagement risk
evaluation process** and a **Client Service Risk Committee (CSRC) review process** whereby quarterly,
McKinsey escalates to CSRC… **all final material recommendations and interim and/or final
deliverables**… identified as 'High Risk'… (hereinafter collectively '**Quality Review Program**')",
plus an independent Compliance Expert who reviews "a **sample of 50 High Risk Engagements**",
including "documentation of any **client disagreement**… and how any such disagreement was resolved."
Risk-triaged escalation, sampled independent inspection, and a record of disagreements — imposed,
not volunteered.

Against that, the audit standard is fully public and has been discussed in §3.6: AS 1220's objective
("**an evaluation of the significant judgments made by the engagement team and the related conclusions
reached**"), its cooling-off rule, its blocking gate ("the firm may grant permission to the client to
use the engagement report **only after** the engagement quality reviewer provides concurring approval
of issuance"), and ISQM 2's date gate ("**the engagement partner is precluded from dating the
engagement report until notification has been received from the engagement quality reviewer**"), with
the escalation valve: if concerns are unresolved, the reviewer notifies the firm that "**the
engagement quality review cannot be completed**."

And the honest coda, from PCAOB inspection: **the review gate itself fails, measurably.** KPMG's 2014
inspection report — public only because the criticisms were not remediated within twelve months —
found:

> "**In 14 of the 24 audits discussed above, the EQCR partner either failed to identify a deficiency
> in an area of significant risk, including in some cases a fraud risk, or he or she failed to pursue
> the matter sufficiently.** … These deficiencies indicate that certain of the Firm's EQCR partners
> did not perform their reviews as thoroughly as necessary, possess the requisite skills, or devote
> sufficient time and attention to their reviews. … The Firm should assess **whether excessive
> workloads or time constraints, or the manner in which related audit documentation is assembled and
> presented to the EQCR partner**, may be contributing to the inadequate reviews."

The second sentence is the design note: **how the work is assembled and presented to the reviewer is
part of the control.** A reviewer given an unnavigable artefact will fail regardless of competence —
which is the case for making the agent emit the claim/evidence table rather than expecting the human
to reconstruct it.

---

## 5. Laboratory and experimental science

Wet-lab QA is where the sibling report's "staged case with a known answer" is not a metaphor but a
line item in a budget. Four of its institutions are worth copying exactly.

### 5.1 An independent QA unit, defined by what it may not be

21 CFR Part 58 (Good Laboratory Practice for Nonclinical Laboratory Studies) makes the independence
of the checker a legal requirement, § 58.35(a):

> "A testing facility shall have a quality assurance unit which shall be responsible for monitoring
> each study to assure management that the facilities, equipment, personnel, methods, practices,
> records, and controls are in conformance with the regulations in this part. **For any given study,
> the quality assurance unit shall be entirely separate from and independent of the personnel engaged
> in the direction and conduct of that study.**"

And § 58.3(l) defines it negatively: "any person or organizational element, **except the study
director**". The QAU's duties are a complete QA-process specification in seven lines — maintain a
**master schedule** of all studies; keep all protocols; **inspect each study at intervals adequate to
assure integrity**, recording "the date of the inspection, the study inspected, the phase or segment
of the study inspected, the person performing the inspection, findings and problems, action
recommended and taken to resolve existing problems, and any scheduled date for reinspection";
escalate anything likely to affect study integrity "immediately"; report status periodically;
"**Determine that no deviations from approved protocols or standard operating procedures were made
without proper authorization and documentation**"; review the final report "to assure that such
report accurately describes the methods and standard operating procedures, and that **the reported
results accurately reflect the raw data**"; and **§ 58.35(b)(7): "Prepare and sign a statement to be
included with the final study report which shall specify the dates inspections were made and
findings reported to management and to the study director."**

That signed statement is the artefact. It does not say the study is right; it says *these checks were
run, on these dates, and this is what was reported*. That is exactly the right shape for an AI QA
pass attached to a card.

SOPs themselves are governed by § 58.81: "**All deviations in a study from standard operating
procedures shall be authorized by the study director and shall be documented in the raw data**…
A historical file of standard operating procedures, and all revisions thereof, including the dates
of such revisions, shall be maintained."

### 5.2 The audit trail, and what "original" means

21 CFR 11.10(e), the sentence the whole electronic-records regime rests on:

> "**Use of secure, computer-generated, time-stamped audit trails to independently record the date and
> time of operator entries and actions that create, modify, or delete electronic records. Record
> changes shall not obscure previously recorded information. Such audit trail documentation shall be
> retained for a period at least as long as that required for the subject electronic records and
> shall be available for agency review and copying.**"

Its paper ancestor is § 58.130(e): "**Any change in entries shall be made so as not to obscure the
original entry, shall indicate the reason for such change, and shall be dated and signed or
identified at the time of the change.**" Note the three fields: *what it was*, *why it changed*,
*who and when*. Most software audit logs record only the third.

**ALCOA** is the mnemonic — Attributable, Legible, Contemporaneous, Original, Accurate — plus
Complete, Consistent, Enduring, Available. The regulators differ on the "+": FDA uses plain ALCOA;
PIC/S PI 041-1 defines all nine; and the MHRA explicitly declines the plus sign while keeping the
attributes: "**The guidance refers to the acronym ALCOA rather than 'ALCOA +'… There is no difference
in expectations regardless of which acronym is used.**" Two MHRA lines are worth lifting straight
into a QA design:

> "An audit trail facilitates the reconstruction of the history of such events relating to the record
> regardless of its medium, including the '**who, what, when and why**' of the action." … "**It is not
> necessary for audit trail review to include every system activity.**"
> "**Data review should be documented and the record should include a positive statement regarding
> whether issues were found or not, the date that review was performed and the signature of the
> reviewer.**"

The second is the sentence to adopt verbatim. *A review that found nothing must still say so, dated
and signed.* Otherwise "no findings" is indistinguishable from "not reviewed" — which is precisely
the Relotius failure mode from §2.2, in a different industry.

FDA's Data Integrity Q&A adds the review rule and the risk-proportionality rule: "**Audit trail review
is similar to assessing cross-outs on paper when reviewing data. Personnel responsible for record
review under CGMP should review the audit trails that capture changes to data associated with the
record as they review the rest of the record**"; and where frequency is not specified, "you should
determine the review frequency… using knowledge of your processes and risk assessment tools. **The
risk assessment should include evaluation of data criticality, control mechanisms, and impact on
product quality.**"

### 5.3 Controls: the known answer you run alongside the unknown one

The wet-lab equivalent of planted truth is the **control**, and its defining property is that it is
processed *identically* to the real sample.

- **No-template control.** MIQE (Bustin et al., *Clin Chem* 2009): "**NTCs detect PCR contamination…
  NTCs should be included on each plate or batch of samples, and conditions for data rejection be
  established.** For example, NTCs with Cqs ≥40 could be ignored if the Cq for the lowest
  concentration unknown is 35." Note the second clause — *the rejection rule is written down in
  advance*, with numbers.
- **Positive control.** MIQE again: "**Positive controls in the form of nucleic acids extracted from
  experimental samples are useful for monitoring assay variation over time and are essential when
  calibration curves are not performed in each run.**"
- **Method blank and matrix spike.** EPA SW-846 Method 8000D: a method blank must be run "each time a
  batch of samples is extracted, cleaned up, and analyzed, and when there is a change in reagents";
  and "at least one matrix spike and one duplicate unspiked sample… with each preparation batch of up
  to **20 samples** of the same matrix". Its diagnostic rule is the elegant part: "**A matrix effect
  is indicated if the LCS data are within limits but the MS/MSD data exceed the limits**" — two
  controls whose *disagreement* localises the fault.
- **Spike recovery bands are concentration-dependent, not a flat 80–120%.** AOAC Appendix F, Table
  A5, gives target mean recovery by analyte level: **98–102% at 100% and 10%; 97–103% at 1%;
  95–105% at 0.1%; 90–107% at 100 ppm; 80–110% in the ppm range; 60–115% at 10 ppb; 40–120% at 1
  ppb.** The tolerance widens as the thing gets harder to measure — a principle that transfers
  directly to how tightly an AI check should be expected to hold.
- **Spike-ins with known ratios.** The ERCC RNA controls are a designed staged case: "**92
  polyadenylated transcripts… traceable through the manufacturing process to the NIST plasmid
  reference material**", in two mixes at four defined molar ratios (**4.00, 1.00, 0.67, 0.50**)
  spanning ~10⁶-fold concentration, so that you can "**compare the Spike-In Mix data to known
  Spike-In Mix concentrations and ratios to assess the dynamic range, lower limit of detection, and
  fold-change response of your platform.**" (96 is the NIST library; 92 is the commercial mix — both
  numbers are right, for different objects.)

### 5.4 Certified reference materials: what a *certified value* actually is

NIST SRMs are the purest form of "a case whose right answer is known before the work starts", and
the certificates say so with unusual care. From SRM 2387 (Peanut Butter):

> "**A NIST certified value is a value for which NIST has the highest confidence in its accuracy in
> that all known or suspected sources of bias have been taken into account.**"
> "**A NIST reference value is a noncertified value that is the best estimate of the true value based
> on available data; however, the value does not meet the NIST criteria for certification and is
> provided with an associated uncertainty that may reflect only measurement reproducibility, may not
> include all sources of uncertainty…**"

Two tiers of known answer, labelled, in the same document. Real values, from SRM 1950 (Metabolites
in Frozen Human Plasma, certificate issued 12 Oct 2023, valid until 30 Sept 2028):

- **Glucose 82.16 ± 1.00 mg/dL** (4.560 ± 0.056 mmol/L)
- **Cholesterol 151.4 ± 3.3 mg/dL**; Creatinine 0.6789 ± 0.0108 mg/dL; Uric acid 4.274 ± 0.089 mg/dL
- **25-Hydroxyvitamin D3 24.27 ± 0.75 ng/g**; α-Tocopherol 8.01 ± 0.22 mg/kg
- **Sodium 141.76 ± 0.31 mmol/L (k = 1.972)**; Potassium 3.665 ± 0.025 (k = 2.064)

and from SRM 2387: **Fat (extractable) 51.6 ± 1.4 g/100 g** certified, while **aflatoxin B1 4.2 ±
0.9 ng/g is a *reference* value, not certified**. Note the three things every certified value
carries that a benchmark "gold answer" usually does not: **an uncertainty with a stated coverage
factor**, **an expiry date**, and **a nullification clause** ("The certified values are nullified if
the material is stored or used improperly, damaged, contaminated, or otherwise modified").

### 5.5 Proficiency testing: a blind unknown, a rule against gaming, and a consequence

External quality assessment is the closest institutional analogue to running a staged case *on the
people doing the work, without telling them which sample it is*. The US statutory version, 42 CFR
493.801(b):

> "**The laboratory must examine or test, as applicable, the proficiency testing samples it receives
> from the proficiency testing program in the same manner as it tests patient specimens.**"
> "**Laboratories that perform tests on proficiency testing samples must not engage in any
> inter-laboratory communications pertaining to the results of proficiency testing sample(s) until
> after the date by which the laboratory must report proficiency testing results to the program.**"
> "**The laboratory must not send proficiency testing samples or portions of proficiency testing
> samples to another laboratory** for any analysis for which it is certified to perform in its own
> laboratory."

Three anti-gaming rules, in the regulation itself: treat it like real work, don't collude, don't
outsource. And the grading, from Eurachem's PT guide (restating ISO 13528):

> **z = (x − X) / σ̂** where x is the participant's result, X the assigned value, σ̂ the standard
> deviation for proficiency assessment. "**a) |z| ≤ 2.0 the score indicates 'satisfactory'
> performance and generates no signal. b) 2.0 < |z| < 3.0 … 'questionable' performance and generates
> a warning signal. c) |z| ≥ 3.0 … 'unsatisfactory' performance and generates an action signal.**"

with a drift detector that has no equivalent in software QA and probably should: investigate after
"**9 consecutive performance scores, for the same parameter, which have the same bias sign against
the assigned value**". Nine results all slightly high is a finding even though none of them failed.

Failure has a defined meaning and a defined consequence. 42 CFR 493.2: "**Unsuccessful participation
in proficiency testing** — (1) Unsatisfactory performance for the same analyte in **two consecutive
or two out of three testing events**…", after which "CMS imposes sanctions". Eurachem's principle:
"**As a basic principle, every unsatisfactory performance score should be investigated and the
investigation documented as this clearly denotes a problem.**"

### 5.6 Assay validation: the named performance characteristics

ICH Q2(R2) (adopted 1 Nov 2023) renamed them "performance characteristics" and defines each:
**accuracy** ("the closeness of agreement between the value which is accepted either as a
conventional true value or as an accepted reference value and the value or set of values measured");
**precision** at three levels — **repeatability** ("the same operating conditions over a short
interval of time"), **intermediate precision** ("intra-laboratory variations… different days,
different environmental conditions, different analysts and different equipment"), **reproducibility**
("the precision between laboratories"); **specificity/selectivity**; **detection limit** ("can be
detected but not necessarily quantitated as an exact value") and **quantitation limit**; **range**;
and **robustness** — "a measure of its capacity to meet the expected performance criteria during
normal use. **Robustness is tested by deliberate variations of analytical procedure parameters.**"

The numbers are worth having because they show what a fully specified acceptance criterion looks
like: "a signal-to-noise ratio of **3:1** … for estimating the DL. For QL, a ratio of at least
**10:1**"; **DL = 3.3σ/S**, **QL = 10σ/S**; repeatability from "a minimum of **9** determinations
covering the reportable range (e.g., 3 concentrations/3 replicates each) or a minimum of **6**
determinations at 100% of the test concentration"; linearity from "a minimum of **five**
concentrations".

FDA's Bioanalytical Method Validation guidance (2018) and ICH M10 (2022) add the run-acceptance rule
that is the archetype of a sampling gate: calibrators within **±15% of nominal, ±20% at the LLOQ**,
with **75% and at least six** non-zero calibrator levels meeting it; and per run, "**≥ 67% of QCs
should be ±15% of the nominal values, ≥ 50% of QCs per level should be ±15%**". The whole run passes
or fails on a proportion, not on every point.

And the one genuinely novel idea for an AI QA design — **incurred sample reanalysis**: "**ISR is a
necessary component of bioanalytical method validation and verifies the reliability of the reported
study sample analyte concentrations**", performed on "**10% reanalysis of the first 1000 samples, and
5% reanalysis of the remaining samples**", with acceptance "67% should be ±20% of the mean". That is
**re-running a sample of the real work later and checking it agrees with itself** — a check that
needs no ground truth at all, only stability. It is the cheapest honest QA available for any process
whose right answer is unknown.

### 5.7 Blinded analysis: hiding the answer from the person who wants it

Klein & Roodman's review (*Annu. Rev. Nucl. Part. Sci.* 55, 2005; open as SLAC-PUB-12051) is the
definitive statement, and its definition is exactly the generalisation this report is looking for:

> "**A blind analysis is a method that hides some aspect of the data or result to prevent
> experimenter's bias.** There is no single blind analysis technique… These methods can be grouped
> according to exactly what is kept hidden: **1. the signal events**… **2. the result**, when the
> numerical answer can be separated from all other aspects of the analysis; **3. the number of events
> in the data set**…; **4. a fraction of the entire data set.**"

The techniques are all cheap and all mechanical: a **hidden signal box** (BNL E791, kept closed until
all selection criteria, efficiencies and background estimates were fixed — "the hidden box should be
chosen somewhat larger than the anticipated signal region"); a **hidden offset** (KTeV computed
ε′/ε(Hidden) = ±1 × ε′/ε + C for a secret constant C and a secret sign, unblinding one week before
publication); **salting** (SNO let an unknown number of cosmic-ray neutrons through a secret window
in its muon veto *and* removed an unknown 20–40% of all events); and **prescaling** (develop on a
known fraction, apply unchanged to the rest).

Two governance rules from the same paper transfer directly:

> "**Collaborations can require that the decision to unblind a result be made as a part of the
> internal review process, with the consultation or approval of a wider sub-group, and not by the
> data analysts alone.**"
> "**The blind analysis method does not require that data analysis stop after unblinding, nor does it
> ensure that the results of the analysis are correct. There is no reason to publish a result known
> to be wrong, just because the analysis was done blindly.**" — the publication should "be explicit
> about **which selections were applied after the unblinding**."

And the limit, stated plainly: "**blind analyses solve one and only one problem, the influence of
experimenter's bias on the measurement**."

**LIGO's "Big Dog" is the canonical fully-staged case**, and it is worth telling in full because it
is the best existing model for "put a person into a staged scenario that simulates the problem".
From LIGO's own account:

> "**A few carefully-selected members of the collaborations would secretly inject some (zero, one, or
> maybe more) signals into the data without telling anyone. The secret goes into a 'Blind Injection
> Envelope', to be opened when the searches are complete.**" The purpose: "**to stress-test the full
> procedure and uncover problems that could not be found in other ways.**"

A strong signal appeared on 16 September 2010, apparently from Canis Major — hence "the Big Dog",
formally GW100916. Six months of analysis produced a paper, *Evidence for the Direct Detection of
Gravitational Waves from a Black Hole Binary Coalescence*, reviewed by an independent Detection
Committee, and "**Everyone voted on whether the work, and all the documentation, was sufficient to
announce the first detection; the result was a unanimous 'yes'.**" The envelope was opened on 14
March 2011 in front of 300 people in the room and 100 more on video: "**There was the event: it was a
blind injection, not the first direct detection of gravitational waves.**" The exercise surfaced bugs
in the injection software, and — the point — those bugs "did not prevent the scientists from finding
the injected event in the first place."

**That is the template.** The scenario was indistinguishable from the real thing; it ran through the
entire pipeline including the human review committee and the draft paper; the answer was sealed in
advance and held by people who were not doing the work; and the unblinding was a scheduled public
event, not a private reveal.

### 5.8 Notebooks, witnessing, pre-registration

The bound-notebook convention is a maker-checker regime with a specific role for the checker: "**The
counter-signatory should sign and date each page of the notebook to confirm that she or he has read
and understood the entry and is satisfied that the entry has been accurately and correctly
written**", and the witness must be "**someone who has read each entry, who is competent to
understand what he or she has read, but who is not a co-inventor**" (Thomson, *IP Management in
Health and Agricultural Innovation*, 2007). NIH's intramural guidance adds a practical constraint
worth noting for agent QA: "**The witness should preferably be a person who is available or can be
easily reached for the next several years.**" ELN vendors list "**Witnessing and freezing (makes note
immutable after the author and a witness have signed it)**" as a discrete feature. (Note that the
legal driver, "swearing behind" a reference under 37 CFR 1.131, applies only to claims with an
effective filing date before **16 March 2013**; the practice outlived its original purpose.)

**Pre-registration** is the staged case run in the other direction: fix the analysis before the data
exist, so nobody can tune the method to the result. OSF: "**Preregistration is the practice of
posting a time-stamped, read-only version of your study plan to a public repository before beginning
data collection or analysis**"; "**Once you submit a registration, you will not be able to edit or
make changes to it**"; and, honestly, "**your preregistration is a plan, not a prison.**" AsPredicted
reduces it to eight questions on one page, of which three are the ones worth stealing for any agent
task specification:

> **5.** "Specify exactly which analyses you will conduct to examine the main question/hypothesis."
> **6.** "Describe exactly how outliers will be defined and handled, and your precise rule(s) for
> excluding observations."
> **7.** "How many observations will be collected or what will determine sample size?" — "**No need to
> justify decision, but be precise about exactly how the number will be determined.**"

("Be precise, no need to justify" is a good instruction for a card's acceptance criteria too.) Its
irreversibility rules are the enforcement: "**We never make any changes to pre-registrations. We
don't fix typos, add information, change answers**"; "**The act of creating a PDF is non-reversible…
even if created in error.**"

The measured effect of moving the gate before the result is large and consistent:

- **Kaplan & Irvin** (*PLoS ONE* 2015;10:e0132382) on 55 large NHLBI trials: "**17 of 30 studies (57%)
  published prior to 2000 showed a significant benefit of intervention on the primary outcome in
  comparison to only 2 among the 25 (8%) trials published after 2000** (χ² = 12.2, p = 0.0005)…
  **Pre-registration in clinical trials.gov was strongly associated with the trend toward null
  findings.**" And the tell: "**almost half of the trials might have been able to report a positive
  result if they had not declared a primary outcome in advance.**"
- **Scheel, Schijen & Lakens** (*AMPPS* 2021): **96% positive results in standard reports vs 43.66%
  in Registered Reports** (though note their own confound — 57.75% of the RRs were replications
  versus 2.63% of standard reports; restricted to original RRs it is 50%).

And the honest counterweight, **Claesen et al.** (*R Soc Open Sci* 2021;8:211037), on how well
pre-registration is actually honoured: "**Two out of 27 preregistered studies contained no deviations
from the preregistration plan. In one study, all deviations were disclosed. Nine studies disclosed
none of the deviations.**" A plan filed is not a plan followed — which is why the RoB 2 signalling
question 5.1 asks specifically whether the analysis plan was "finalized before unblinded outcome data
were available."

### 5.9 What the replication projects actually found

The headline numbers, each verified against the primary source, because they are routinely misquoted:

| Project | Finding, verbatim where quoted |
|---|---|
| **Open Science Collaboration 2015** (psychology, 100 studies) | "**Ninety-seven percent of original studies had statistically significant results. Thirty-six percent of replications had statistically significant results**; 47% of original effect sizes were in the 95% CI of the replication effect size; 39% of effects were subjectively rated to have replicated… Replication effects were **half the magnitude** of original effects." |
| **Reproducibility Project: Cancer Biology** (Errington et al., *eLife* 2021) | Planned 193 experiments from 53 papers; completed **50 experiments from 23 papers**. "**None of the 193 experiments were described in sufficient detail in the original paper to enable us to design protocols to repeat the experiments.**" Original authors were "not at all helpful or non-responsive" for **32%** of experiments. ~**197 weeks** and ~**$52,574** per replication. Of 136 originally-positive effects, **43% (42/97 tested)** were statistically significant in the same direction; median replication effect **85% smaller**. |
| **Many Labs 1** (13 effects, 36 samples) | "**10 effects replicated consistently.** One effect… showed weak support… **two effects… did not replicate.**" (10 of 13, not 11.) |
| **Many Labs 2** (28 findings, 125 samples, 15,305 participants) | "**fifteen (54%) of the replications provided evidence in the same direction and statistically significant**" at p<.05; **14 (50%)** at p<.0001. Median original d = 0.60; median replication d = **0.15**. |
| **Many Labs 5** | Effects were "**78% smaller, on average, than the original effect sizes**", and expert-revised protocols produced results essentially identical to the unrevised ones. |
| **Social Sciences Replication Project** (21 *Nature*/*Science* studies) | "**a significant effect in the same direction as the original study for 13 (62%) studies**, and the effect size of the replications is on average about **50%** of the original." Peer prediction markets correlated with outcome at **ρ = 0.777**. |
| **Economics** (Camerer et al., *Science* 2016, 18 studies) | "**a significant effect in the same direction… for 11 replications (61%); on average, the replicated effect size is 66% of the original.**" |
| **Nature survey 2016** (n = 1,576) | "**More than 70% of researchers have tried and failed to reproduce another scientist's experiments, and more than half have failed to reproduce their own experiments**"; 52% call it "a significant crisis", but "less than 31% think that failure to reproduce published results means that the result is probably wrong". |
| **Amgen** (Begley & Ellis, *Nature* 2012) | 53 "landmark" haematology/oncology papers; "**scientific findings were confirmed in only 6 (11%) cases.**" |
| **Bayer** (Prinz et al., *Nat Rev Drug Discov* 2011) | 67 projects; "**only in ~20–25% of the projects were the relevant published data completely in line with our in-house findings.**" |

The single most important line for a QA-support feature is not any of the percentages. It is
Errington's: **none of 193 published experiments contained enough detail to be repeated**. The
binding constraint on verification is almost never the checker's capability; it is that the producer
never recorded what they actually did. Everything in §§1–4 about "support for judgement", annotated
manuscripts, working papers and issue logs is a response to that one fact.

---

## 6. AI agents doing research and knowledge work — what is used as the oracle

The honest reading of this literature: **every benchmark that works picks a task where the answer
already exists, and hides it.** The differences between them are differences in where the known
answer comes from, and in whether anyone checked the checker.

### 6.1 Five oracle families

| Oracle | How the known answer is obtained | Examples |
|---|---|---|
| **Executable check against gold tests** | Scientist-written reference solution plus test cases | SciCode (80 main / 338 subproblems), SUPER, DS-1000 |
| **Reproduction of a numeric result within a tolerance** | The paper's or capsule's own output | CORE-Bench (95% prediction interval from 3 manual runs), MLE-bench (Kaggle *private* leaderboard medal thresholds), RE-Bench (continuous score normalised to start=0, reference solution=1) |
| **Short-answer equivalence, graded by a model** | Question engineered so the answer is one short string | GAIA (quasi-exact match), BrowseComp and HLE (a shared judge prompt), DSBench, BixBench |
| **Hand-written hierarchical rubric, graded by an LLM judge** | Experts decompose "did it work" into hundreds of binary leaves | PaperBench, ResearchRubrics, AstaBench report tasks, DeepResearch Bench (RACE) |
| **Blinded human expert pairwise preference** | No oracle at all — only a comparison | GDPval, ScienceAgentBench's rubric pass, RE-Bench's human baseline |

### 6.2 PaperBench: the rubric as a reusable artefact

PaperBench (OpenAI, arxiv.org/abs/2504.01848, Apr 2025) asks agents to replicate 20 ICML 2024
Spotlight/Oral papers from scratch, and grades against **8,316 individually gradable leaf nodes**.
Each rubric "was written in collaboration with one of the original authors of each paper, and took
multiple weeks per paper." Decomposition stops at the point where "an expert human could review
whether a submission satisfies it in **less than 15 minutes**". Node weights "indicate the importance
of that contribution relative to its siblings, and **not necessarily the node's implementation
difficulty**."

Seven design choices worth lifting wholesale into a card-level QA schema:

1. **The root of the rubric is the card's claim.** For the `bam` paper the root reads
   `"The core contributions of the paper \"Batch and match: black-box variational inference with a
   score-based divergence\" have been reproduced."`, weight 1, with four children (weights 2, 2, 1,
   1). I counted the shipped tree on 2026-09-21: 1,021 nodes, 789 leaves; summed over the 23 paper
   directories now in the repo, **8,921 leaves**.
2. **Every leaf is typed by what kind of evidence settles it.** `Code Development` ("does the code
   contain a correct implementation of this?"), `Code Execution` ("does running `reproduce.sh` lead
   to this being successfully executed?"), `Result Analysis` ("did the `reproduce.sh` execution
   produce evidence that agrees with these results?"). In `bam`: 255 / 518 / 16 leaves respectively.
   The paper's prose calls these Code Development / Execution / **Result Match**; the shipped JSON
   and judge code use **"Result Analysis" / "Code Execution"**. Worth knowing before quoting either.
3. **The judge sees different files per leaf type.** Result Match leaves are shown the docs,
   `reproduce.sh`, `reproduce.log` and the reproduction outputs **but not the source code** — so a
   claim about a result cannot be satisfied by reading the implementation.
4. **Re-execution happens on a fresh machine.** The submission is copied to a clean VM and
   `reproduce.sh` is run there, which "ensure[s] replication outputs can be distinguished from any
   results hard-coded by the candidate at task-time." No `reproduce.sh` ⇒ **0 on all Execution and
   Result Match nodes.**
5. **Contamination control is an explicit file.** `bam/blacklist.txt` is one line —
   `https://github.com/modichirag/GSM-VI`, the authors' own repo. A post-hoc monitor searches agent
   logs for blacklisted URLs; **10 of 646 runs were disqualified** and scored 0.
6. **The judge is itself benchmarked.** *JudgeEval* is built from partial replications of four
   benchmark papers and one dev paper, **manually graded leaf by leaf** as ground truth. Macro F1:
   random 0.49, GPT-4o 0.73, o1-mini 0.78, **o3-mini 0.83 at $66/paper**, o1 0.84 at $830/paper.
   The headline runs use o3-mini — the judge was chosen on cost per unit of measured agreement.
7. **Cheap proxies were measured against the real thing and found weak.** PaperBench Code-Dev skips
   reproduction entirely ($10/paper instead of $66) and correlates with the full benchmark at only
   **Pearson r = 0.48**.

Scores: best BasicAgent **Claude 3.5 Sonnet (New) 21.0 ± 0.8%**; with IterativeAgent (submit tool
removed, forced to use the full budget) **o1-high 24.4 ± 0.7%**, 26.0% at 36 hours. Human baseline:
8 ML PhD students/holders, 3 attempts each, timesheet-tracked — **41.4% (best of 3) after 48 hours
on a 3-paper subset, vs 26.6% for o1 on the same subset**. The shape of the curve is the finding:
"o1 initially outperforms the human baseline during the early stages… but humans start outperforming
the AI agent **after 24 hours**"; o1 "mostly plateau[s] after the first hour."

### 6.3 What "AI scientist" systems actually ship as verification

- **Sakana's AI Scientist** (arxiv.org/abs/2408.06292) writes papers at "less than $15 per paper"
  and evaluates them with **its own automated reviewer**, claimed to achieve "near-human performance
  in evaluating paper scores". The headline — papers that "exceed the acceptance threshold at a top
  machine learning conference" — is explicitly *as judged by our automated reviewer*. That is a
  closed loop: the producer's sibling is the judge.
- The one external datapoint they sought was real, and their own caveats are the useful part. A v2
  paper, *Compositional Regularization: Unexpected Obstacles in Enhancing Neural Network
  Generalization*, scored **6, 7, 6 (avg 6.33)** at the ICLR 2025 ICBINB workshop, roughly the top
  45% of submissions, at a venue with 60–70% acceptance. 1 of 3 submissions passed; "none of the 3
  papers passed our internal bar for what we believe would qualify as an accepted ICLR conference
  track paper"; and "Even if papers by The AI Scientist were accepted, we would withdraw them before
  they were actually published." (sakana.ai/ai-scientist-first-publication/)
- **Google's AI co-scientist** (research.google blog, Feb 2025) runs a Supervisor plus Generation,
  Reflection, Ranking, Evolution, Proximity and Meta-review agents, with an **Elo tournament fed by
  self-play scientific debate** as the internal judge. Its most-quoted validation is a staged case in
  exactly the sibling report's sense: the system "independently proposed that cf-PICIs interact with
  diverse phage tails to expand their host range" — a hypothesis **already experimentally validated
  and unpublished** by collaborators at the Fleming Initiative and Imperial College London. The
  answer existed before the run and was withheld. The other two validations (AML drug repurposing in
  vitro; liver-fibrosis epigenetic targets in human hepatic organoids at Stanford) were wet-lab
  checks *after* the fact, not staged cases.
- **PaperQA2** (arxiv.org/abs/2409.13740) is the strongest citation-grounded design, and reports its
  contradiction-detection result the right way round: "identifies 2.34 ± 1.99 contradictions per
  paper in a random subset of biology papers, **of which 70% are validated by human experts**." The
  30% that are not is the number a QA design has to plan around.

### 6.4 Measuring whether a claim is actually supported by its citation

This is the most directly reusable body of work, because it defines "grounded" operationally.

**AIS** (Rashkin et al., *Computational Linguistics* 49(4), 2023, aclanthology.org/2023.cl-4.2.pdf)
gives the definition everything else builds on. Verbatim:

> A pair (s, t) consisting of a standalone proposition s and a time t is Attributable to Identified
> Sources (AIS) iff the following conditions hold: 1. The system provides a set of parts P of some
> underlying corpus K, along with s. 2. (s, t) is attributable to P. A pair (s, t) is attributable
> to a set of parts P of some underlying corpus K iff: A generic hearer will, with a chosen level of
> confidence, affirm the following statement: **"According to P, s"**, where s is interpreted
> relative to time t.

The "according to" test is the whole trick — and it is the same test a *New Yorker* checker applies
by hand.

- **ALCE** (Gao et al., EMNLP 2023) turns it into two numbers. *Citation recall* is 1 for a
  statement iff it has at least one citation and an NLI model (TRUE, T5-11B) says the concatenated
  cited passages entail it; *citation precision* is per-citation, penalising irrelevant citations but
  not requiring a minimal set. Validated: Cohen's κ **0.698** (recall) and **0.525** (precision)
  against humans; **85.1% / 77.6%** accuracy against human gold labels. Headline: on ELI5, "even the
  best models lack complete citation support 50% of the time."
- **FActScore** (arxiv.org/abs/2305.14251): decompose into atomic facts, score % supported;
  automated estimator has "less than a 2% error rate"; ChatGPT biographies were **58%** supported.
- **SAFE** (arxiv.org/abs/2403.18802): decompose, then Google-search each fact. Agrees with
  crowdworkers **72%** over ~16,000 facts and, on 100 sampled disagreements, **SAFE won 76%** — the
  automated checker was right more often than the humans it disagreed with — at >20× lower cost.
- **FACT**, the citation half of DeepResearch Bench, is the deployed version of the same idea:
  extract (statement, URL) pairs, deduplicate, fetch the page, and ask a judge for a **binary
  support / not-support** verdict; report both **citation accuracy** and **effective citations per
  task**. The results table shows why both are needed: Claude-3.5-Sonnet with search scores **94.04%
  citation accuracy over 9.78 effective citations**, Gemini-2.5-Pro Deep Research **81.44% over
  111.21**. Abundance and trustworthiness trade off. FACT's judge *was* validated, on a random
  sample of 100 statement-URL pairs: Gemini-2.5-Flash "aligned with human 'support' determinations
  in **96%** of cases and with 'not support' determinations in **92%** of cases."

Against that, the field measurement: the Tow Center / CJR study (March 2025, *AI Search Has a
Citation Problem*) ran 1,600 queries (20 publishers × 10 articles × 8 tools), asking each tool to
identify the source of a verbatim excerpt. **Over 60% of responses were incorrect**; Perplexity 37%,
Grok-3 94%. ChatGPT Search answered 134 of 200 incorrectly while signalling uncertainty **15 times**
and never declining. Grok-3 returned 154 of 200 citations to error pages.

Two lessons for a QA feature: citation *existence* and citation *support* are different checks, and
**stated confidence is not evidence**. BrowseComp measures this directly: calibration error rises
with capability — GPT-4o 69%, GPT-4o with browsing 82%, Deep Research **91%** at 51.5% accuracy —
and the paper's own gloss is that "access to web tools may increase the model's confidence in
incorrect answers."

### 6.5 LLM-as-judge: what it is good for, and its pathologies

The founding claim (Zheng et al. 2023, arxiv.org/abs/2306.05685) is that GPT-4 as judge reaches
"over 80% agreement" with controlled and crowdsourced human preferences — "the same level of
agreement between humans". The same abstract names the failure modes: **position bias, verbosity
bias, self-enhancement bias, limited reasoning ability.**

What the last two years added:

- **Human inter-rater agreement on open-ended knowledge work is about 68–71%.** This number recurs
  independently: DeepResearch Bench's expert annotators agree with each other **68.44%** of the time;
  AstaBench's instance-level human agreement is **68.1%**; GDPval's expert inter-rater agreement is
  **71%**. That is the ceiling any judge is measured against, and it caps what any of these
  benchmarks can resolve.
- **Narrow decidable checks get much higher agreement than holistic ones — this is the single most
  important empirical fact for QA design.** DeepResearch Bench's FACT step asks one binary question
  per (statement, URL) pair — *does this page support this sentence?* — and its judge agreed with
  humans **96% / 92%**. The same paper's holistic report-quality judge (RACE) tops out at **71.33%**,
  because the humans themselves only agree **68.44%**. Decompose far enough and a model can be
  trusted; ask for an overall verdict and it cannot, because nothing can.
- **Where holistic judges are validated, they land at roughly the human ceiling.** PaperBench o3-mini **F1 0.83**
  against hand-graded leaves; DeepResearch Bench's RACE reaches **71.33%** pairwise agreement with
  humans, *above* the humans' own 68.44%; GDPval's automated grader hits **66%, "only 5% below human
  expert inter-rating agreement of 71%"**; ResearchRubrics reports Macro F1 **0.72–0.76**.
- **But most judges are not validated at all.** DiscoveryBench's four-stage LLM hypothesis-matching
  score (HMS), DSBench's semantic-equivalence comparator, BixBench's Claude judge and
  ScienceAgentBench's GPT-4o figure judge report **no human-agreement statistic** (ScienceAgentBench
  cites prior work instead). If there is one methodological claim to carry into a product decision,
  it is this: *an unvalidated LLM judge is an opinion with a number attached.*
- **Partial credit makes agreement worse.** ResearchRubrics (arxiv.org/abs/2511.07685) graded the
  same rubrics binary and ternary: collapsing to binary raised human–judge Macro F1 by roughly 20
  points, "confirming that partial credit introduces ambiguity without improving discriminative
  power."
- **Rubrics are biased toward whoever's output seeded them.** AstaBench ran a hold-out study: three
  of five systems whose outputs had contributed rubric "ingredients" dropped significantly (average
  2.5 points, p ≤ 0.01) when held out of rubric construction; with only four contributing systems
  the drop was 5 points.
- **Self-preference is real and measurable.** Panickssery et al. (arxiv.org/abs/2404.13076): "an LLM
  evaluator scores its own outputs higher than others' while human annotators consider them of equal
  quality", with "a linear correlation between self-recognition capability and the strength of
  self-preference bias". *Never let the agent that did the card judge the card.*
- **A panel beats one big judge.** PoLL (arxiv.org/abs/2404.18796): a panel of smaller models from
  *disjoint families* "outperforms a single large judge, exhibits less intra-model bias … while being
  over seven times less expensive."
- **Self-correction without external feedback does not work.** Huang et al.
  (arxiv.org/abs/2310.01798): "LLMs struggle to self-correct their responses without external
  feedback, and at times, their performance even degrades after self-correction." A "review your own
  work" step is not QA.
- **Critic models help, but hallucinate; the team is the right unit.** CriticGPT
  (arxiv.org/abs/2407.00215): model critiques preferred over human critiques in **63%** of cases on
  naturally-occurring LLM bugs; but critics produce "hallucinated bugs that could mislead humans into
  making mistakes they might have otherwise avoided", and "human-machine teams of critics and
  contractors catch similar numbers of bugs to LLM critics **while hallucinating less than LLMs
  alone**."
- **Process supervision beats outcome checking.** Lightman et al. (arxiv.org/abs/2305.20050):
  "Process supervision significantly outperforms outcome supervision"; the process-supervised model
  "solves 78% of problems from a representative subset of the MATH test set"; PRM800K is **800,000
  step-level human labels**. Translated to knowledge work: grading the *chain of work* (did it screen
  every record; did it check each citation against its source) is more informative and more
  correctable than grading only the finished artefact — which is what PaperBench's leaf typing does
  in practice.

### 6.6 GDPval: what happens when there is no oracle

The most relevant 2025 entry for knowledge work generally is OpenAI's **GDPval** (Sep 2025):
44 occupations across the top 9 US GDP sectors, tasks built from real work product by professionals
averaging **14 years of experience**, 220-task open gold subset. Its verdict on automated grading is
the point: "Given the complexity of automatically grading these tasks, our primary evaluation metric
is **head-to-head human expert comparison**" — blinded pairwise ranking of unlabeled deliverables,
three graders per sample, **averaging over an hour per comparison**. Their automated grader was then
validated against that: **66% agreement with human expert graders, only 5% below the human expert
inter-rating agreement of 71%.** Headline: **47.6% of Claude Opus 4.1 deliverables were graded better
than or as good as the human deliverable.**

The lesson for a QA feature is not the score. It is that for the general knowledge-work case, the
best-funded attempt to build an oracle concluded that the oracle is a blinded second expert — and
then spent its automation budget on *measuring how close a model gets to that*, rather than on
replacing it.

### 6.7 Cost, which decides what is actually shippable

Every rubric oracle in this literature is expensive. PaperBench: multiple weeks per paper with the
original author to write the rubric, "tens of hours per paper" to hand-grade, $66/paper to
LLM-grade. ResearchRubrics: 2,800+ hours of human labour for 2,500+ rubric items. GDPval: over an
hour per pairwise comparison. This is exactly why cheap proxies get built — and why PaperBench
Code-Dev's **r = 0.48** against the full benchmark is the most important number in the section. A
cheap checker that correlates at 0.48 with the thing you care about is not a gate; it is a hint.

---

## 7. Cross-cutting principles

### 7.1 Independence, and the four grades of it

Every regime in sections 1–6 has the same shape: **the checker must not be the maker, and must not
depend on the maker.** Cochrane wants two screeners who have not seen each other's decisions; SR
11-7 wants validation "done by people who are not responsible for development or use and do not have
a stake in whether a model is determined to be valid"; GLP wants a QA unit "entirely separate from
and independent of the personnel engaged in the direction and conduct of that study"; ISO 17100
requires "**a person other than the translator**"; a fact-checker is a different person from the
writer and can overrule them.

The most precisely graded version of this anywhere is the US Department of Energy's *Human
Performance Improvement Handbook* (DOE-HDBK-1028-2009, Volume 2), which distinguishes four tools and
says which is stronger and why:

> "Verification practices refer broadly to four tools: peer-checking; concurrent verification;
> independent verification; and peer review… While **peer-checking (PC) focuses on preventing a
> mistake by the performer**, independent verification (IV) and concurrent verification (CV) focus
> more on **confirming the correct configuration or status** of equipment or documents."
> "'**Checking**' refers to the confirmation of a correct action – prevention of an error by a
> performer. '**Verification**' refers to the confirmation of the condition of equipment or accuracy
> of documents…"

- **Self-checking (STAR: Stop, Think, Act, Review)** — the performer's own pause. "Stop – Pause before
  performing critical activities. Eliminate distractions… Think – Understand what will happen when
  correct action is taken on the correct component… **Identify expected outputs/results of the
  action** … Act – Perform the correct action on the correct component… Review – **Verify that
  outputs or results match the expected outputs/results.**" *Expectation stated before the action,
  compared after* — the same structure as ISA 520's analytical procedures.
- **Peer-check** — "**a series of actions by two individuals working together at the same time and
  place**… **PC is the least rigorous of the checking and verification tools.**" Its risk list is a
  catalogue of how four-eyes fails: "Peer is reluctant to correct a more senior performer" ·
  "Performer or peer does not self-check rigorously, **assuming the other person will**" · "Performer
  is less attentive to the action, believing the peer will catch any problems" · "**PC is over-used,
  eventually leading to complacency by both parties.**"
- **Concurrent verification** — same time, same place, separate confirmation, and an explicit
  admission of its limit: "**The performer and verifier attempt to create freedom of thought between
  them**… **Because CV requires both individuals to work together, side by side, true independence
  cannot be achieved.**"
- **Independent verification** — "**one individual, separated by time and distance from the action…
  confirms the condition of the component or document.** … **The IV process tends to have a higher
  probability of catching an error than PC or CV, because the verifier is not involved in changing
  the component's state or producing the document and their knowledge of the system, component, or
  work situation is unaffected by the performer.** … **Separation by distance is established when
  audible or visual cues of either person are not detectable by the other person.**"

That last definition is the operational test for an AI checker: *did the checker see the maker's
work, or only its output?* The AI evidence says the same thing twice over — self-correction without
external feedback does not improve reasoning (Huang et al. 2023), and a judge model prefers its own
generations (Panickssery et al. 2024). So the minimum viable design is **a different model, from a
different family, that did not see the maker's reasoning trace** — PoLL's disjoint-family panel, not
a "critique yourself" pass. Anything less is concurrent verification at best: useful, but the source
says true independence is not achievable that way.

DOE also supplies the protocol for handing information between the two, **three-way communication**:
sender states the message; "the receiver **repeats back the message in a paraphrased form**…
**Equipment designators and nomenclature are repeated word for word**"; sender responds "**That is
correct**" or "**That is wrong**" and restates. Paraphrase to show understanding, *verbatim* for the
identifiers. That is a good rule for a card handoff too.

### 7.2 Sampling with materiality, not 100% review

100% human re-review of agent output defeats the point of the agent, and no mature profession does
it. Audit does not re-add the ledger: it sets **materiality** (ISA 320), draws a sample (ISA 530),
**projects the misstatement to the population** (¶14), and escalates only if the projection crosses
the threshold. Manufacturing does not inspect every unit: ANSI/ASQ Z1.4 (formerly MIL-STD-105E) fixes
an **acceptable quality limit** and derives a sample size from the lot size — for a lot of 501–1200
units at General Inspection Level II and AQL 1.0%, sample-size code letter **J**, **n = 80**,
**accept on 2, reject on 3** — and accepts or rejects *the lot*. Translation LQA samples 500–20,000
words and counts weighted errors per 1000.

Three properties make sampling legitimate, and all three have to be engineered:

1. **The population must be homogeneous and enumerable.** You can sample 30 of 400 extracted data
   points because they are the same kind of decision. You cannot "sample" the conclusion of a memo.
2. **The result must be projected, and there must be a rejection rule.** ISA 530 ¶14 requires
   projection; MIL-STD-105E ships *switching rules* — normal inspection tightens "when 2 out of 2, 3,
   4, or 5 consecutive lots have been rejected", and relaxes "when 5 consecutive lots have been
   considered acceptable". A sample with no consequence is theatre.
3. **The sample must be drawn the way the population will be used.** MIL-STD-105E's own §3.1 warning
   is the most under-appreciated sentence in acceptance sampling: "**The AQL alone does not identify
   the chances of accepting or rejecting individual lots or batches but more directly relates to what
   might be expected from a series of lots or batches.**" An AQL characterises a *process*; it says
   nothing about any individual item. The same is true of any pass rate you compute over agent
   outputs.

The counter-case is explicit everywhere: where a single defect is catastrophic, you verify 100%.
Aviation calls those **killer items**; ISA 450 calls the boundary **clearly trivial**; MQM lets a
single **Critical** error trigger an automatic Fail regardless of the score.

And the one form of sampling that needs no ground truth at all is worth naming separately:
**incurred sample reanalysis** (FDA BMV 2018) — re-run 10% of the first 1,000 real samples and 5% of
the rest, and require that 67% agree within ±20% of the mean of the two runs. It checks the process
against *itself*, later. For agent work whose right answer is unknown, this is the cheapest honest
check available.

### 7.3 Checklists, and the DO/CHALLENGE distinction

Degani & Wiener's NASA report (*Human Factors of Flight-Deck Checklists: The Normal Checklist*, NASA
CR-177549, 1990) draws the distinction most software checklists get wrong. Verbatim, p.19:

> **Challenge-Response.** "In this method, which can be more accurately termed
> '**challenge-verification-response**,' the checklist is a **backup for the initial configuration**
> of the plane. Here, the pilots use their memory and other techniques to configure the plane. After
> completing the initial configuration, the pilots use the checklist to **verify that several
> critical items have been correctly accomplished**."
> **Do-list.** "This method can be better termed '**call-do-response**.' In this method, the checklist
> is used to 'lead' and direct the pilot in configuring the aircraft using a step-by-step, 'cook
> book' approach. Therefore, **the configuration redundancy employed in the challenge-response method
> is eliminated here.**"

In a 1989 Boeing tabulation of 20 airlines, 12 used challenge-and-response, 7 combined, 1 do-list.
The warning about do-lists is exactly the failure mode of an AI "QA checklist" that the same agent
fills in as it works: "due to the elimination of the configuration redundancy, **a mistake can easily
pass unnoticed once the sequence is interrupted**."

**Killer items** appear twice, both times as an observation about how people actually behave.
Short-haul crews running a checklist 10–30 times a trip perform "only what he/she perceives as the
critical items ('**killer items**' as some call them)" (p.20) — the long checklist decays into the
short one by itself. And the designer's response (p.51): "**duplication of a very few highly critical
items ('killer items') that are based on possibly transient data, can be beneficial.**"

The design rule that falls out: **a QA checklist should be challenge-response — run after the work,
by someone other than the worker, verifying rather than directing — and short enough that it is
actually run.** Mark the two or three kill items explicitly; expect everything else to be skipped
under time pressure and design for that rather than against it. ResearchRubrics' **+4/+5 "Critically
important — required for a minimally viable response"** band is the same idea expressed as a weight.

The efficacy evidence is Haynes et al., *NEJM* 2009;360(5):491-9 — a 19-item checklist across 8
hospitals, 3,733 patients before and 3,955 after: "**The rate of death was 1.5% before the checklist
was introduced and declined to 0.8% afterward (P=0.003). Inpatient complications occurred in 11.0% of
patients at baseline and in 7.0% after introduction of the checklist (P<0.001).**"

### 7.4 Provenance, and the "experienced stranger" test

The recurring artefact across every field here is not the verdict but **the trail that lets a later
stranger re-derive it**. The standards state it as a reader test:

- ISA 230 ¶8 / PCAOB AS 1215 ¶.06A — documentation sufficient "to enable **an experienced auditor,
  having no previous connection with the audit**, to understand" the procedures, the results, and the
  significant judgments; and ¶9 — record "**who performed the audit work and the date… and who
  reviewed the audit work performed and the date and extent of such review**".
- 21 CFR 11.10(e) — "**computer-generated, time-stamped audit trails**… **Record changes shall not
  obscure previously recorded information.**"
- Cochrane MECIR C54/C55 — a free-text **"Support for judgement"** and the **source of information**
  for every risk-of-bias judgement, because "readers, editors and referees should have the
  opportunity to see for themselves from where supports for judgements have been obtained".
- The *Spiegel* commission's requirement — sources marked on the galley, documents attached, sorted
  and marked, *or the check does not run*.
- DESNZ's issue log — `Date found` and `Date shared with model owner` as **separate** columns.

They are all the same object: **claim → evidence → who decided → when**. For a card-level QA pass this
is the cheapest thing to get right and the one that compounds: if the agent emits a row per claim
with a pointer to the artefact that settles it, the human pass becomes reviewing rows rather than
re-reading the work. Errington's finding from the Cancer Biology project — **none of 193 published
experiments contained enough detail to be repeated** — is the cost of getting it wrong.

One rule deserves to be adopted verbatim, from the MHRA's data-integrity guidance: "**Data review
should be documented and the record should include a positive statement regarding whether issues were
found or not, the date that review was performed and the signature of the reviewer.**" Without the
positive statement, "no findings" and "not reviewed" are the same record — which is exactly how
Relotius survived a checking department of fifty.

### 7.5 Confidence and uncertainty as required, separate fields

Mature regimes force the producer to state confidence *separately from* the claim, and to distinguish
**how likely** from **how good my basis is**. GRADE's four certainty levels sit beside the effect
estimate, with a footnote per downgrade. eLife rates *significance* and *strength of evidence* on two
independent ladders. NeurIPS asks for a confidence score whose anchors describe **what the reviewer
actually did** ("5: …checked the math/other details carefully" … "1: Your assessment is an educated
guess… Math/other details were not carefully checked"). RoB 2's *Yes / Probably yes / Probably no /
No / No information* separates "firm evidence is available" from "a judgement has been made".

Forecasting is the only field that scores it. The IARPA tournaments measured "more than 150,000
forecasts of 743 participants on 199 events occurring over 2 years" (Mellers et al., *J Exp Psychol
Appl* 2015;21(1):1-14) and found the discriminating behaviours were training in probabilistic
reasoning, deliberation time, and **frequency of belief updating** — not seniority.

The contrast with current agents is stark, and it is measured. BrowseComp reports calibration error
*rising* with capability — GPT-4o 69%, GPT-4o with browsing 82%, Deep Research **91%** at 51.5%
accuracy — with the authors' gloss that "access to web tools may increase the model's confidence in
incorrect answers." In the Tow Center study, ChatGPT Search was wrong on 134 of 200 queries and
hedged 15 times. **An agent's stated confidence is an output to be scored later, not a gate now.**

### 7.6 Conformance versus correctness — the cut that decides who checks

The single most useful distinction across all seven areas, and the one the empirical evidence
supports most directly:

- **Conformance to a standard** — did it follow PRISMA; is every CONSORT item on a page; do the
  p-values match their test statistics; does the flow diagram balance; does the model's trial balance
  net to zero; is every citation resolvable and entailing; does every variable in the codebook appear
  in the data; is there an NTC on the plate. These are **decidable from the artefact**, need no
  judgement, and are exactly what an AI checker should do first, exhaustively.
- **Whether the conclusion is right** — is this the right comparator; is the identifying assumption
  plausible; is the framing of the brief the one the reader needs; is the hypothesis worth the bench
  time. These are **not decidable from the artefact**, and no rubric rescues them.

JPL drew this line in 1976, in one sentence, about one check (*The Levels of Edit*, NASA CR-146584):

> "An Integrity Edit is concerned primarily with ensuring that the parts of a publication match. For
> example, if 'Figure 1' is cited, an Integrity Edit will determine whether Figure 1 is included in
> the report. **However, it will not determine whether Figure 1 is actually the figure that is cited
> in the text.** Such a determination, which depends on the meaning of the text and the meaning of
> the figure, is included in Substantive Edit."

The 2026 evidence puts numbers on it. Ask a model **one binary, decidable question** — *does this page
support this sentence?* — and it agrees with humans **96% / 92%** (DeepResearch Bench's FACT step,
validated on 100 statement-URL pairs). Ask it for a **holistic quality verdict** and it tops out
around **71%**, because the humans themselves only agree **68.4%** (RACE), **68.1%** (AstaBench) or
**71%** (GDPval's expert inter-rater agreement). ResearchRubrics found that merely collapsing a
three-level rubric to binary raised human–judge agreement by ~20 points.

**Decompose far enough and a machine can be trusted. Ask for an overall verdict and it cannot,
because nothing can.** statcheck is the clean illustration in the other direction: it recomputes
p-values from the reported statistic and df and found that "half of all published psychology papers
that use NHST contained at least one p-value that was inconsistent", and one in eight a *gross*
inconsistency — while saying precisely nothing about whether the study was worth doing. Both checks
are necessary; only the first can be automated to completion.

Three corollaries worth stating explicitly:

- **Scope statements are part of the check.** Forvis Mazars publishes "what we do not do" alongside
  "what we confirm", and says why: "**A clear scope is not a narrow one. It is what makes the opinion
  worth relying on.**" DESNZ requires a time-boxed check to "**communicate that the model has not been
  fully QA'd in a clearance statement.**" A QA pass that does not say what it did not check is
  unreadable.
- **Prevention is an order of magnitude cheaper than correction**, and this is measured: resolving an
  image-integrity concern *after* publication cost *Molecular and Cellular Biology* ~**6 hours** of
  staff time per paper; screening before publication cost ~**30 minutes** per problematic paper.
- **A clean check is a finding.** The *Spiegel* commission's diagnostic was the "**weiße**", very
  empty manuscript. GLP requires a signed statement of inspections *performed*, not of problems
  found. Design the artefact so that "nothing found" is an affirmative, dated, attributable record —
  or the absence of findings will be read as the absence of risk.

---

## (a) Kind of knowledge work → who judges → what can be staged → what AI does first → what is left to a person → how the verdict is recorded

| Kind of work | Who/what is the judge | Stageable with a known answer? | What an AI checker can do first | The question left to a person | How the verdict is recorded |
|---|---|---|---|---|---|
| **Literature review** (narrative, not systematic) | A domain reader; increasingly a citation-grounding checker | **Partly — gold set.** Plant 5–10 papers the reviewer must have found (known key references, plus 2 that must be excluded); also plant one **retracted** paper to test the retraction check | Resolve every DOI; check every claim entails from its cited passage (ALCE-style NLI / AIS "according to P, s"); flag retractions; flag quotation drift; list cited-but-unread | Is the framing right? Is the literature the *relevant* literature, or the findable one? Whose work is missing because nobody cites it | Per-claim table: claim → citation → supported / unsupported / not checkable, plus a coverage note naming what was searched |
| **Systematic screening pass** (title/abstract) | Two independent human screeners; disagreements to a third (Cochrane MECIR **C39**, mandatory) | **Yes — gold set.** A held-out set of known includes/excludes from a completed review; measure **recall**, not accuracy | Screen everything; report recall against the gold set and the stopping rule used. Calibration: a single human screener misses **13%** of relevant studies, two miss **3%** (Gartlehner et al. RCT, 2020). Cheapest high-value design: agent screens all, a human re-screens only the **excludes** (Cochrane RR R14/R15) | Sign off on the stopping point: is 95% recall enough *for this question*? Adjudicate disagreements | PRISMA flow counts (identified → screened → excluded → sought → assessed → included) — which must balance arithmetically — plus, per PRISMA item 8, **how many reviewers, whether independent, and which automation tools**; and the recall achieved on the gold set |
| **Written research report** | An editor, then a fact-checker, then a reader | **Partly — injected defect.** Seed the draft with a wrong number, a mis-transcribed quote and a citation that does not support its sentence; see whether the checker finds all three | Every number traced to the source cell/table; every quote character-matched; every citation entailment-checked; internal consistency (abstract vs body vs tables); reporting-guideline conformance | Is the conclusion warranted by the evidence presented? Is the emphasis honest? What is the reader going to do with it | Annotated draft: each factual sentence marked verified / unverifiable / disputed, with the source; plus a query list back to the author |
| **Regression write-up** | A replication package reviewer (AEA-style); a referee | **Yes — planted truth + injected defect.** Simulate data with a known coefficient; separately, break one line of the code and check the pipeline notices | Re-run from raw data to final table; check every reported number appears in the log; statcheck-style recomputation of p-values/CIs from the reported statistics; check N's add up across tables | Is the identifying assumption plausible? Is this the right comparator/specification? Would the result survive a different reasonable choice | Reproduction log: table/number → script → line → matched / differs by x; plus a statcheck-style inconsistency list |
| **Financial model** | An independent model reviewer / model audit; four-eyes sign-off | **Yes — injected defect + reference case.** Plant errors of known types (hardcode in a formula, off-by-one range, sign flip, broken link) and measure detection; run a case whose answer is known analytically | Formula-consistency scan across each row; hardcode detection; range/precedent tracing; checks-tab integrity (balance sheet balances, cashflow ties); recalculation; sensitivity sweep for non-monotonicity and #REF/#DIV | Are the *assumptions* right? Is the structure the right representation of the deal? What did nobody model | Issue log rows: ID, sheet!cell, description, category, severity, impact, raised-by, status, resolution |
| **Legal memo** | A cite-checker, then a supervising attorney | **Yes — injected defect.** Insert one non-existent case, one real case cited for a proposition it does not support, and one case that has been overruled; the checker must find all three | Verify every citation exists; verify the quoted proposition appears at the pinpoint; run the negative-treatment signal (KeyCite red/yellow/orange-overruling-risk, Shepard's stop sign/Q/triangle); string-match every quotation (Westlaw's QuoteRight does exactly this); check signal appropriateness and Bluebook form | Is this the right legal theory? What is the other side's best argument? Is the risk assessment the client needs | Cite-check packet: one row per citation — exists / pinpoint verified / **supports the proposition it is cited for** / treatment signal / form corrected; signed and dated. (The operative failure mode is *misgrounded*, not fabricated: a real citation attached to a claim it does not support.) |
| **Policy brief** | A peer reviewer, then a red team / murder board | **No known answer.** Only an injected-defect test of the *sourcing* layer | Source every factual claim; separate claims from assumptions; check that stated confidence matches the evidence cited; flag unsourced causal language | Is the recommendation right, given values and politics? What are the competing hypotheses and have they been fairly treated? What happens if the key assumption is wrong | A sourcing annex (claim → source → confidence), a key-assumptions list, and a written record of the alternative analysis considered and rejected |
| **Grant proposal** | A study section / panel, scored against named criteria | **No** for merit. **Yes** for conformance: stage a proposal with known violations (page limits, missing biosketch, unaddressed criterion) | Conformance: every required section present, limits respected, each scored criterion explicitly addressed, budget arithmetic, prior-support statements | Significance, innovation, and whether the approach will work — the whole point of the panel | Criterion scores on the published scale (NIH: 1 Exceptional … 9 Poor, on Significance / Investigators / Innovation / Approach / Environment) plus a written critique; overall impact is *not* the average |
| **Lab protocol / SOP** | A QA unit independent of the study; the next person to run it | **Yes — reference material / spike-in / control.** Run the protocol on a sample with a certified or assigned value; include positive and negative controls | Completeness against the SOP template; every reagent has a catalogue number and lot field; every step has an acceptance criterion; units and concentrations consistent; version and approval fields present | Will this protocol actually work at the bench, and is it safe? Is the control the right control | Signed and dated protocol version; a QA-unit inspection record naming date, phase inspected, inspector, findings and re-inspection date (21 CFR 58.35(b)(3)); control results (positive control passed, no-template control clean, recovery within the concentration-appropriate band); and a signed statement of **which inspections were performed**, whether or not anything was found |
| **Data dictionary / codebook** | A data archive's curator; the next analyst | **Yes — the data itself is the oracle.** Every claim in the codebook is checkable against the file | Every variable in the data appears in the codebook and vice versa; value labels cover every observed value; ranges and types match; missing codes reconcile; N's match; derived-variable formulas re-derive | Are the variable definitions *meaningful*? Does the documented universe match what was actually collected? What should a reuser be warned about | A conformance report (variable → present / labelled / range-consistent / missing-codes-covered) attached to the deposited dataset |
| **Translation** | A second linguist. ISO 17100 §5.3.3: "The reviser, **who shall be a person other than the translator**, shall… examine the target language content against the source language content for any errors" | **Yes — injected defect + reference.** Seed known mistranslations, omissions and terminology violations at known severities; also use a certified reference translation | Terminology-base conformance; untranslated segments; number/date/name/locale format; omissions and additions by alignment; consistency across segments | Register, tone, and whether the text reads as if written in the target language for this audience; whether a deliberate deviation was right | MQM error log: span, category, severity (Neutral 0 / Minor 1 / Minor-Fluency-Punctuation 0.1 / Major 5 / Non-translation 25) → weighted error score per 1000 words against a pass threshold |
| **Meeting summary** | The attendees; formally, approval of the minutes at the next meeting | **Yes — staged transcript.** A scripted meeting with a known decision list, known action owners and one deliberately ambiguous exchange | Every named decision, owner and date traced to a transcript timestamp; flag any summary sentence with no supporting transcript span. Base rates to design against: ~**1%** of Whisper transcriptions contain wholly hallucinated phrases, **38%** of those carrying explicit harms (Koenecke et al., FAccT 2024); in ambient clinical scribes, **omissions are the most common error and the hardest for the author to catch**, because spotting them requires recalling what was said (Biro et al., *JMIR* 2025: 127 errors across 44 notes, 70% of notes affected) | Did the meeting actually mean that? What was decided implicitly? What was said that should *not* be minuted | Minutes approved (or amended) at the next meeting; and, for the AI pass, a per-sentence transcript-span citation with unsupported sentences flagged |
| **Slide deck** | The partner / the audience; there is no standards body | **Weakly — injected defect only.** Plant a number inconsistent with the appendix, a chart whose axis contradicts its caption, and a claim with no backup slide | Every number on a slide ties to a backup slide or source cell; chart data matches the underlying table; axis/scale honesty checks; claim–evidence pairing; consistency of the storyline (each headline follows from its body) | Is this the right story? Does the "so what" land for *this* audience? What will the first question be | Review marks on the storyline/ghost deck, and a tie-out sheet: slide → number → source |
| **Reproduction of a paper** | The rubric, graded leaf by leaf; a human validates the grader | **Yes — known published result**, with the authors' own code blacklisted | Re-run `reproduce.sh` **on a clean machine** so hard-coded results cannot pass; grade each rubric leaf (code development / execution / result analysis); report per-leaf pass with the evidence that settled it; enforce a blacklist so the authors' own code cannot be consulted | Did the reproduction fail because the agent is wrong or because the paper is wrong? Is a near-miss the same result | Weighted rubric tree score, with each leaf's judgement and its supporting artefact (log line, figure, file) |
| **Hypothesis generation** | The wet lab, eventually; in the meantime, a domain expert panel | **Yes, and this is the best staged case there is** — a hypothesis the lab has already confirmed but not published (the cf-PICI case). Withhold it and see whether the system proposes it | Novelty check against the literature; internal consistency; is it falsifiable; is the proposed experiment adequately powered and controlled; has it already been done | Is it worth doing? Is it interesting? Which of the twenty plausible hypotheses gets the bench time | Ranked list with, per hypothesis, the supporting literature, the proposed discriminating experiment, and a reviewer's judgement; an Elo/tournament rank is an internal signal only |
| **Forecasting memo** | The future; before that, a calibration record | **Yes, deferred — resolution criteria.** Unambiguous resolution criteria written before the event turn an opinion into a scoreable claim | Check every forecast has a numeric probability, a resolution date and unambiguous resolution criteria; check base rates cited; check for internal incoherence (probabilities that do not sum, conditionals that contradict) | Is the estimate right? What has been left out of the model of the world | The forecast record itself, then a Brier/log score at resolution and a calibration curve over the forecaster's history |

---

## (b) Artefact formats worth borrowing verbatim

Each of these is a real, published artefact with a real example. They are listed in the order a card
would meet them: what was searched, what was judged, what was checked, what was reported, what was
graded.

### 1. PRISMA flow counts — a reconciliation ledger that must balance

Real example — Ermiati et al., *Int J Women's Health* 2026;18:1–8 (doi:10.2147/IJWH.S616559), from
the figure's own long description:

> Identification: Records identified from Pubmed (n = 5), Scopus (n = 8), ScienceDirect (n = 19),
> Garuda (n = 15), totaling 47 records. Records removed before screening: Duplicate records removed
> (n = 6). Screening: Records screened (n = 41). Records excluded: Inappropriate title (n = 18),
> inappropriate abstract (n = 1). Reports sought for retrieval (n = 22). Reports not retrieved:
> Irretrieval full text (n = 4). Reports assessed for eligibility (n = 18). Reports excluded:
> Combined intervention (n = 3), protocol study (n = 1), review study (n = 3), qualitative study
> (n = 1). Included: Studies included in review (n = 10).

**What to borrow:** every node's outflow must equal its inflow minus its named exclusions, and every
exclusion must carry a reason with a count. It is an arithmetic identity, so a machine can verify it
completely — and published diagrams frequently fail it. Add the PRISMA 2020 footnote as a field:
"**If automation tools were used, indicate how many records were excluded by a human and how many
were excluded by automation tools.**"

### 2. A risk-of-bias judgement row with its support-for-judgement quote

Real example — Ballouk et al., *BDJ Open* 2025;11 (doi:10.1038/s41405-025-00294-z), Table 6, the row
for Anauate-Netto et al. 2014 under *Bias arising from the randomization process*:

> **Authors' judgement:** Low risk
> **Support for judgement:** "A computer-generated list of random numbers was used. Rinses were
> prepared in dark bottles which were consecutively numbered according to the randomization
> schedule. Participants were randomized to one of the three test color-matched rinses. Study
> coordinator, examiners, and participants were unaware of group allocation".

and the same study under *Bias in selection of the reported result*:

> **Authors' judgement:** Low risk
> **Support for judgement:** The measurements of outcomes described in the methods section were all
> reported.

The Cochrane RoB 1 table format is literally three columns — `Bias | Authors' judgement | Support
for judgement` — and it records *absence* explicitly rather than leaving cells blank (Patton et al.,
CDSR 2024, CD009362.pub4):

```
Random sequence generation (selection bias)  | Unclear risk | Randomised clinical trial but method of randomisation not described in sufficient detail
Allocation concealment (selection bias)      | Unclear risk | Not stated
Blinding of participants and personnel       | High risk    | Without blinding
Selective reporting (reporting bias)         | Unclear risk | Trial not registered. Therefore, it is unclear if all planned outcomes were measured.
```

**What to borrow:** three things. (i) The **verdict and its evidence live in the same row** — you
cannot record one without the other. (ii) A **quoted** support and an **asserted** support are
visibly different (quotation marks), which distinguishes "the source says so" from "the assessor
inferred it". (iii) MECIR C55's rationale is the reason: *"Readers, editors and referees should have
the opportunity to see for themselves from where supports for judgements have been obtained."*

The scale beneath it is worth borrowing too — RoB 2's signalling-question responses are **Yes /
Probably yes / Probably no / No / No information**, where "The definitive versions ('Yes' and 'No')
would typically imply that firm evidence is available…; the 'Probably' versions would typically
imply that **a judgement has been made**." That is a two-bit scale that separates *what I found*
from *what I concluded*.

### 3. A fact-checker's annotated sentence

Real example — John McPhee, "Checkpoints", *The New Yorker*, 9 Feb 2009, reporting what checker Anne
Stringfield had on her proof and what it became:

> On the proof: *"Just above Cromwell's Falls on Route 3, very close to but not visible from the
> river, is a Budweiser brewery that has a production average of **thirteen thousand kegs a day**."*
> Checked: the real average was **eighteen thousand kegs in a day**. (The number had been "made up
> out of thin air".)

The convention the annotation follows, from the same piece, quoting checker Sara Lippincott:

> "**Each word in the piece that has even a shred of fact clinging to it is scrutinized, and, if
> passed, given the checker's imprimatur, which consists of a tiny pencil tick.**"

**What to borrow:** the four dispositions, which together are a complete state machine for a claim.
**Ticked** (verified against a source). **On author** (rests on the writer's own witness; not
independently checkable; recorded as such). **TK** (a promissory placeholder — "`name TK, number
TK`… These are forms of promissory note, and a checker is expected to pay it"). **Deleted** — "If
unverifiable, it would be deleted."

Add Spiegel's fifth state and its inverse signal: the Dokumentation "**markiert, welche Fakten
schlicht nicht verifiziert werden konnten**" (marks which facts simply could not be verified), and
the Relotius commission's diagnostic was the *absence* of marks — "**das Phänomen der »weißen«, also
sehr leeren Manuskripte**", very empty manuscripts with little corrected, "which is highly unusual".
**An unusually clean check is itself a finding.**

### 4. A CONSORT checklist item with its page reference

Real example — CONSORT 2010, item 6a, with the standard column layout
`Section/Topic | Item No | Checklist item | Reported on page No`:

> **Outcomes | 6a | "Completely defined pre-specified primary and secondary outcome measures,
> including how and when they were assessed" | ___**

PRISMA 2020 calls the last column `Location where item is reported`; CARE calls it `Reported on
Line`. But the version worth copying is PLOS Medicine's submission requirement, which upgrades the
cell from a pointer to evidence:

> "**Authors must complete the appropriate reporting checklist not only with page references, but
> also with sufficient text excerpted from the manuscript to explain how they accomplished all
> applicable items.**"

**What to borrow:** a checklist cell should hold the **quoted text that satisfies the item**, not a
page number. A page number is unverifiable without the artefact; a quotation is verifiable by string
match. This single change turns a self-report into a decidable check.

### 5. An audit working-paper tick-mark legend

The rule first, verbatim from the IRS *Internal Revenue Manual* 4.10.9.8.2 (04-22-2024):

> **Tick marks are used to simplify documentation of conditions found and work performed. Tick marks
> do not need to be standardized throughout the case file, but they must be consistent throughout the
> workpapers for an issue. Tick mark explanations must be a part of the workpaper or included in a
> separate tick mark legend workpaper.**

A real two-symbol legend, from PSASB Kenya's *Template 29: Working Paper* (Internal Audit Standards,
Public Sector Entities), verbatim:

> "Legend should be used to explain tick-marks and symbols used"
> **Tick mark legend:**
> **✓** – Test / audit procedure was performed successfully on sample item and no exceptions were noted.
> **✗** – An exception was noted when test / audit procedure was performed on sample item.
>
> and, cross-referenced from the same block:
> **N1** – We were unable to obtain the specimen signature for Mr XYZ… Refer to …… for factual
> correctness sheet signed by the Procurement Manager.
> **N2** – The specimen signature… does not agree to the signature on the purchase order.

And a minimal machine-friendly one, from the US DoD FIAR *Test Plan / Evaluation of Test Results*
template, verbatim:

> **Tick Mark Legend** — **0** No Exception Noted · **1** Exception Noted

with the methods named on the same sheet: "**The four types of tests are: Inquiry, Observation,
Examination, and Reperformance.**"

**What to borrow:** three things, in this order of importance. (i) **The legend travels with the
artefact** — a symbol without its legend on the same page is meaningless, so a QA pass should emit
its own key. (ii) **Consistency is required within a unit of work, not globally** — which is exactly
right for per-card checks, where a global taxonomy would be wrong within a month. (iii) **Exceptions
get a letter and a sentence** (N1, N2), not just a mark: the mark says *there is something here*, the
note says *what*. Note also that the classic legend usually quoted in textbooks ("^ = footed, ✓ =
agreed to supporting documentation, TB = agreed to trial balance") could **not** be traced to any
primary source in this research; the verifiable legends are the plain ones above.

Every working paper also carries the same metadata block, which is the audit-trail minimum and is
required by ISA 230 ¶9 / PCAOB AS 1215 ¶.06: the item tested, **who performed the work and the date
it was completed**, and **who reviewed it, when, and to what extent**.

### 6. A model-review issue-log row

Real example — the UK Department for Energy Security and Net Zero's published QA workbook
(`DESNZ_Quality_Assurance__QA__modelling_full_log_template.xlsm`), `Issues Log` tab, header row
verbatim:

```
Model Version | Unique Formula Reference | QA analyst | Worksheet | Cell/Range |
Comments | Priority | Impact | Notes | Date found (use ctrl+;) |
Date shared with model owner | Issue has been addressed?
```

with the controlled vocabularies from its `Lookups` tab, verbatim:

```
Rating                 Weight      Priority  Weight    Importance     Weight
1) Excellent             0         High        9       Essential        3
2) Good                  1         Medium      3       Important        2
3) Some issues           3         Low         1       Best Practice    1
4) Needs improvement     7
5) Significant issues   10         Worst value 10
```

**What to borrow:** the two date columns. `Date found` and **`Date shared with model owner`** are
separate fields, and `Issue has been addressed?` is a third state — so the log distinguishes *found*
from *reported* from *fixed*, which is the minimum needed to tell whether a QA process is working or
merely running. Also borrow the numeric weights: a severity word that carries a weight can be summed
into a score, and DESNZ does exactly that (`Model score` tab, with a stated target: "Each model
should aim to score either **90%** (if it's business-critical) or **85%** if it's not
business-critical").

Pair it with the honest-scope habit from the same guidance, for time-boxed checks: at 0.5 days or
less, "**Communicate that the model has not been fully QA'd in a clearance statement.**"

### 7. An ICD 203 confidence statement

The rule first, verbatim from Intelligence Community Directive 203, *Analytic Standards* (2 Jan 2015):

> "To avoid confusion, products that express an analyst's confidence in an assessment or judgment
> using a 'confidence level' (e.g., 'high confidence') **must not combine a confidence level and a
> degree of likelihood, which refers to an event or development, in the same sentence**."

with the mandatory likelihood vocabulary ("an analytic product must use one of the following sets of
terms"):

| almost no chance | very unlikely | unlikely | roughly even chance | likely | very likely | almost certain(ly) |
|---|---|---|---|---|---|---|
| remote | highly improbable | improbable | roughly even odds | probable | highly probable | nearly certain |
| 01–05% | 05–20% | 20–45% | 45–55% | 55–80% | 80–95% | 95–99% |

A real one, from ICA 2017-01D (6 January 2017), showing the two axes and a recorded dissent on the
*confidence* axis only:

> "We further assess Putin and the Russian Government developed a clear preference for President-elect
> Trump. **We have high confidence in these judgments.**"
> "We also assess Putin and the Russian Government aspired to help President-elect Trump's election
> chances… **All three agencies agree with this judgment. CIA and FBI have high confidence in this
> judgment; NSA has moderate confidence.**"

and the definitions that make "high confidence" mean something, from the same document's Annex B:

> "**High confidence** generally indicates that judgments are based on high-quality information from
> multiple sources. **High confidence in a judgment does not imply that the assessment is a fact or a
> certainty; such judgments might be wrong.**
> **Moderate confidence** generally means that the information is credibly sourced and plausible but
> not of sufficient quality or corroborated sufficiently to warrant a higher level of confidence.
> **Low confidence** generally means that the information's credibility and/or plausibility is
> uncertain, that the information is too fragmented or poorly corroborated to make solid analytic
> inferences, or that reliability of the sources is questionable."

**What to borrow:** three separate fields — **what might happen** (likelihood, from a fixed
vocabulary with numeric bands), **how good my basis is** (confidence: high / moderate / low, defined
in terms of *sources*, not feelings), and **who disagrees**. Plus the prohibition on mixing them in
one sentence, which is enforceable by a regex. An agent that writes "I am highly confident this is
very likely" has said one thing twice and nothing once.

### 8. A PaperBench rubric leaf

From `project/paperbench/data/papers/bam/rubric.json` in github.com/openai/frontier-evals (formerly
openai/preparedness), fetched 2026-09-21 — one leaf of 789 in that file:

```json
{
  "id": "bam-qm-equation",
  "requirements": "The 'match' step in BaM has been implemented such that at each iteration, the U and V matrices have been implemented using the accumulated statistics from the 'batch' step, as outlined in equations (10) and (11) and in step 6 of Algorithm 1.",
  "weight": 2,
  "sub_tasks": [],
  "task_category": "Code Development",
  "finegrained_task_category": "Method Implementation"
}
```

and the comparable *Result Analysis* leaf — note that it asserts **a relationship between numbers**,
not a number:

```json
{
  "requirements": "The forward and reverse KL divergence between target and estimated Gaussian distributions measured over VI iterations for BaM and ADVI show that BaM converges orders of magnitude earlier (in terms of number of iterations) than ADVI.",
  "weight": 1,
  "task_category": "Result Analysis",
  "finegrained_task_category": "Evaluation, Metrics & Benchmarking"
}
```

The judge is told what kind of evidence settles each type (`TASK_CATEGORY_QUESTIONS`, verbatim):

```
"Code Development": "Does the code in the submission contain a correct implementation of this?
   Note: mere instructions or descriptions of implementations are not sufficient; you should check
   whether there is actual code that implements the requirement."
"Code Execution":  "Does running the reproduce.sh script lead to this being successfully executed?"
"Result Analysis": "Did the reproduce.sh script execution produce evidence that agrees with these results?"
```

and is forced into a three-part answer (`GRADING_PROMPT`, verbatim):

> "**# Expectations** — Read the paper carefully to identify the parts relevant to your criterion.
> Describe what you expect correct resolution to look like… **# Reality** — Explore the files provided
> for the submission along with the output logs… **Be sure to describe only what has been given to
> you in the submission**, and be explicit about which files you are referring to… **# Score** —
> Reason about whether the criteria has been met. Give a score of either 0 or 1…
> - You must always provide a score. If you have any uncertainties, make them clear in your discussion.
> - **All the files and logs from the submission attempt have been provided to you. If anything appears
>   to be missing, assume that the attempt failed to produce it (treat it as a failure).**"

**What to borrow:** the four leaf fields (a requirement in one testable sentence; a weight *relative
to siblings* meaning importance not difficulty; a category naming what kind of evidence settles it;
`sub_tasks` so the tree bottoms out at something one reviewer can check in **under fifteen minutes**);
the **Expectations-before-Reality** grading order, which is ISA 520's "set the expectation first" in
prompt form; the default-to-failure rule for missing evidence; and the sibling file `blacklist.txt` —
one line, `https://github.com/modichirag/GSM-VI`, the authors' own repo — because a staged case only
works if the answer cannot be looked up. A post-hoc monitor disqualified **10 of 646 runs** for
touching blacklisted URLs.

### Two more worth stealing

#### (a) A signed rubric weight scale — how to mark a kill item

From arxiv.org/abs/2511.07685, Table 4 (verbatim):

| Score range | Description |
|---|---|
| **[+4, +5]** | **Critically important** — A criterion without which the response is fundamentally flawed or incorrect. **Required for a minimally viable response.** |
| **[−5, −4]** | **Critically detrimental** — A criterion identifying an error so severe that it makes the response actively harmful, deeply unethical, or completely invalidates its reasoning. |
| [+2, +3] | Important — A key feature of a strong response, but not absolutely essential. |
| +1 | Slightly Important — A "nice-to-have" detail… |
| −1 | Slightly Detrimental — A minor issue, tangent, or stylistic weakness… |
| [−3, −2] | Detrimental — A significant error that detracts from the response quality… |

and a real rubric triple for one prompt (Figure 1):

```
1) The response identifies at least 5 societal domains (e.g., mental health, relationships,
   politics/civic engagement, the information ecosystem, the economy).                      +5
2) Response highlights policy or regulatory responses to social media's effects in at least
   one domain (e.g., Section 230 of the CDA, COPPA, the SAFE act, New York's Child Data
   Protection Act).                                                                         +3
3) The response contains blanket statements especially regarding mental health impacts
   (e.g., body dysmorphia, increased anxiety, emotional distress triggers, disruption of
   sleep patterns) without citations.                                                       −4
```

**What to borrow:** *signed* criteria, so a rubric can say "this must not be there" as well as "this
must"; and the ±4/±5 band as the explicit marking of aviation's kill items. Also their measured
result: collapsing partial credit to binary raised human–judge Macro F1 by ~20 points.

#### (b) An MQM error row — the translation QA artefact, with its arithmetic

Operationalised in Freitag et al., *Experts, Errors, and Context*, TACL 2021
(aclanthology.org/2021.tacl-1.87/). The annotator instruction, verbatim:

> Please identify all errors within each translated segment, up to a maximum of five. If there are
> more than five errors, identify only the five most severe… To identify an error, highlight the
> relevant span of text, and select a category/sub-category and severity level from the available
> options… Please pay particular attention to document context when annotating. If a translation
> might be questionable on its own but is fine in the context of the document, it should not be
> considered erroneous; conversely, if a translation might be acceptable in some context, but not
> within the current document, it should be marked as wrong.

and the weighting (Table 4, verbatim):

| Severity | Category | Weight |
|---|---|---|
| Major | Non-translation | 25 |
| Major | all others | 5 |
| Minor | Fluency/Punctuation | 0.1 |
| Minor | all others | 1 |
| Neutral | all | 0 |

**What to borrow:** an error log where each row is *(span, category, severity)* and the score is
**derived**, never entered. The 0.1 weight for minor punctuation is the interesting design detail:
it lets a nit be recorded without letting nits outvote a mistranslation. The cap at five errors per
segment and the single Non-translation escape hatch are the same idea as "don't grade a garbled
answer item by item".

---

## (c) What "put a person into a staged case" generalises to

The sibling report's staged case — *a case whose right answer is known before the code runs* — is a
special case of a more general move, and the general move is what decides the shape of QA for any
card.

**The move is: manufacture an asymmetry between the checker and the maker.** The maker has to
produce the answer; the checker only has to compare. Everything that works in sections 1–7 creates
that asymmetry in one of three ways, and which one is available is a property of the *work*, not of
how hard you are willing to try.

**1. There is a stageable known answer — when the deliverable's truth is external and already
determined somewhere.** This holds far more often than it looks, and not only for results. A gold
set of included studies stages a screening pass. A proficiency-testing sample with an assigned value
stages an assay. A NIST reference material stages a measurement. LIGO's blind injection stages a
whole collaboration's analysis pipeline, paper and all. A retracted paper planted in a reference
list stages a citation check. A deliberately wrong figure in a model stages a model review. A
question whose answer is short, checkable and hard to find stages a research agent (BrowseComp). A
hypothesis the lab has already confirmed but not published stages a hypothesis generator (Google's
cf-PICI case). The common precondition is that **the answer can be withheld** — which is why
PaperBench ships a `blacklist.txt`. If the agent can look it up, it is not a staged case, it is a
retrieval test. The second precondition is that **the staged case has to fail the way the real work
fails**: an injected defect that no plausible error would produce tells you nothing about the
checker you actually have.

**2. Sampling with materiality is right when the work is a large population of similar decisions.**
Data extraction, screening, cite-checking, tie-outs, translated segments, coded variables, extracted
table cells — each is hundreds or thousands of instances of one decision type. Here you do not need
a known answer at all: you need a *rate*, a threshold, and a rejection rule. Audit's shape (set
materiality, sample, project, escalate) and manufacturing's (AQL, sample size from lot size, accept
or reject the lot) both work, and both are honest because the population is homogeneous and the
consequence of a failed sample is defined. This is the default shape for agent work, because agent
work is usually high-volume repetition of one judgement. The failure mode to avoid is sampling with
no rejection rule — reviewing ten of two hundred extractions, finding two wrong, and shipping
anyway.

**3. Only a second independent mind will do when the deliverable is a judgement about what matters.**
Is this the right comparator; is the framing of the brief the one the reader needs; is the hypothesis
worth a year of lab time; is the risk the memo does not mention the one that will actually bite.
These are not decidable from the artefact, which is exactly why peer review, murder boards, devil's
advocacy, analysis of competing hypotheses and engagement quality review all exist as *people*
rather than as checklists. Note what these do: they do not check the answer, they **attack the
reasoning** — and they are staffed by someone with, in SR 11-7's phrase for effective challenge, the
incentive, competence and influence to overturn it. No LLM judge substitutes here, partly because
self-preference is measurable and partly because "is this worth doing" has no ground truth to
validate a judge against.

**The practical consequence for a QA-support feature.** For each card, the first question is not
"what tests do we run" but **"which of the three is available?"** — and the answer is usually
*more than one, at different layers*. A regression write-up: planted truth stages the estimator
(layer 1), sampling checks the table-to-code correspondence (layer 2), a person judges whether the
identification strategy is credible (layer 3). A systematic review: a gold set stages the screener,
sampling checks the extractions, a person judges the GRADE certainty rating. The tooling should make
layer 1 and layer 2 cheap and exhaustive, record them as rows a human can scan, and **spend the
human's attention only on layer 3** — stating, explicitly, that layers 1 and 2 passed and what they
did not cover. The worst outcome is the one the AI-scientist systems keep producing: a closed loop
where the only judge is a sibling of the maker, reporting a score that no external case ever
constrained.

---

## What could not be verified

Listed because a QA report that does not say what it failed to check is exactly the artefact this
report argues against. The session's web-search budget was exhausted at the start, so everything was
obtained by direct URL fetch; several primary sources are behind paywalls or bot-blocks.

**Paywalled or blocked, so quoted from a restatement rather than the original:**

- **The Bluebook's own text** (Rule 1.2 signals, Rule 18.2) — legalbluebook.com is behind a JS
  challenge. Signal definitions are quoted from Cornell LII's *Basic Legal Citation* and the CC0
  Indigo Book, which restate them.
- **The IAASB's international ISA text** — ifac.org/iaasb.org 404 on every standard. All ISA
  quotations in §3.6 are from the **FRC's ISA (UK)** editions, which reproduce the IAASB text with UK
  insertions; two numbering divergences are flagged there (ISA 500's procedure paragraphs; ISA 530's
  MUS appendix). AICPA AU-C sections are gated and were not consulted at all.
- **ISO 13528 / ISO/IEC 17043** — the z-score thresholds in §5.5 are quoted from the Eurachem PT
  guide, which restates them, not from ISO.
- **ISO 17100 / ISO 18587** — iso.org 403s; clause 5.3.3 was read from a full-text mirror and
  structurally verified clause-by-page against ISO's own official preview table of contents.
- **CLSI EP05/EP06/EP09/EP15 study designs** — paywalled; the LoB/LoD formulas in §5.6 come from
  Armbruster & Pry (*Clin Biochem Rev* 2008), a free peer-reviewed restatement, not from EP17.
- **Landis & Koch 1977** (the κ bands) and **Balshem 2011** (GRADE 3) — paywalled; corroborated from
  open-access papers that reproduce them and from the GRADE Handbook respectively.
- **MacCoun & Perlmutter, "Blind analysis: Hide results to seek the truth"** (*Nature* 2015) — metadata
  confirmed, full text unobtainable. No quotation from it appears here; Klein & Roodman is the open
  substitute.
- **McKinsey** — mckinsey.com refused every attempt. Nothing in §4.8 is attributed to the firm; the
  seven steps and the "day-1 answer" are Conn & McLean's wording. **MECE** could not be sourced at all.
- **COPE** — publicationethics.org 403s every path, so no COPE guidance is quoted in §2.5.

**Numbers deliberately not repeated because no primary source was found:**

- The frequently quoted claim that superforecasters beat intelligence analysts with classified access
  "by about 30%" — traces to press reporting and a trade book, not a retrievable paper.
- Any figure for the **PubPeer statcheck posting campaign** (e.g. "50,000 papers").
- A **count of US federal judges with AI standing orders** — the RAILS tracker is JS-only and stopped
  updating in May 2025. Do not publish a "200+" figure.
- The classic US audit **tick-mark legend** ("^ = footed", "TB = agreed to trial balance") — not found
  in any primary source; §(b) quotes the two legends that *were* verifiable instead.
- The **origin of the ALCOA acronym** — widely attributed to Stan W. Woollen (FDA, 2010), but none of
  MHRA, FDA, WHO or PIC/S attributes it to anyone, and the cited article is unobtainable.
- The **Morgan & Morgan internal email** reported alongside *Wadsworth v. Walmart* — press-only, not
  in the order.

**Corrections to claims commonly repeated in this area, each verified against the primary source:**

- The Cochrane Handbook does **not** contain "safety first" or "liberal accelerated" screening;
  "liberal accelerated" is Khangura et al., *Syst Rev* 2012.
- Cochrane **discourages** kappa as a quality measure ("We do not recommend the use of statistical
  measures of agreement… It is more important that reasons for any disagreement are explored and
  resolved"), Handbook §7.3.2.
- PaperBench's shipped JSON uses `task_category` values **"Result Analysis" / "Code Execution" /
  "Code Development"**, not the paper's prose names.
- **DeepResearch Bench's FACT judge *was* validated** (96% / 92% agreement on 100 statement-URL
  pairs); only its holistic RACE counterpart sits near the human ceiling.
- **Many Labs 1 replicated 10 of 13** effects, not 11; **Many Labs 2** is 15/28 at p<.05 and 14/28 at
  p<.0001.
- The **SSRP**'s replication effect size is ~50% of the original; the 71% figure applies to true
  positives only.
- PCAOB AS 1215's documentation completion date is now **14 days**, not 45 (Release 2024-004); **QC
  1000 is not yet effective** (delayed to 15 Dec 2026), so AS 1220 is not superseded.
- **Judge Starr's certification order has been rescinded** and replaced by N.D. Tex. LR 7.2(f), which
  inverts it into a disclosure rule.
- **Proposed FRE 707 is not on track**: "The Committee does not recommend action on the proposed
  Rule 707 at this time" (Advisory Committee, May 2026).
- **AOAC's spike-recovery bands are concentration-dependent** (98–102% at percent levels, 40–120% at
  1 ppb); "80–120%" is a trace-level rule of thumb, not a universal criterion.
- **AsPredicted prints eight questions**, though the site advertises "nine simple questions".
- **PLOS ONE criterion 1** reads "original research"; the anti-importance statement is on the
  *reviewer guidelines* page, not the criteria page.
- **NeurIPS's 1–10 "Award quality" ladder is the 2024 form**; 2025 uses 1–6 overall plus four 1–4
  sub-scores. The 1–5 confidence scale is unchanged.
- **CONSORT 2025** dropped the "Reported on page No" column from the statement's own table; it
  survives only in the separate fillable checklist.
- The claim that **John Paul II abolished the devil's advocate** is not supported by *Divinus
  Perfectionis Magister*, which converts rather than abolishes the role.
