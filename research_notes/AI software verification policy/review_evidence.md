# AI-assisted software verification: evidence for a 550-card backlog

Research checked 2026-09-24. “Measured” denotes an empirical observation or experiment; “guidance” denotes a source's prescription; “policy inference” is a proposed decision rule for this backlog, not a demonstrated effect. Source dates are publication dates where identified; living documentation is marked accessed 2026-09-24.

## How effective is AI-assisted code review, and what does a clean AI review prove?

### Takeaway
AI review is useful as a fast source of candidate findings, but published evidence does not justify treating an AI review with no comments as proof of correct or secure behavior. The strongest direct security evaluation found important missed vulnerability classes.

### Cited Findings
- **Measured, direct AI review evaluation (Amro & Alalfi, 25–29 May 2026):** On curated, labeled vulnerable code from multiple languages and projects, GitHub Copilot code review frequently missed SQL injection, cross-site scripting, and insecure deserialization, while tending to comment on style and typos. The abstract does not provide a generalizable sensitivity estimate for real pull requests. [Conference paper](https://proceedings.mlr.press/v318/amro26a.html).
- **Measured, observational (Hirao et al., posted 2025-08-26):** A study of 16 GitHub AI review actions, over 22,000 comments, and 178 repositories found widely varying rates of resulting code changes; concise, code-specific and manually triggered feedback was more likely to lead to changes. “A code change followed” is an action proxy, not proof that the comment was correct or a defect fixed; its outcome classifier was itself LLM-assisted. [Study](https://arxiv.org/abs/2508.18771).
- **Measured, observational (2026 preprint):** A study of 54,791 agent review comments in 342 Python repositories found incorrect suggestions and intentional design decisions among common reasons comments remained unresolved. Resolution is not correctness. [Study](https://arxiv.org/abs/2607.21997).
- **Vendor documentation, not an effectiveness study (accessed 2026-09-24):** GitHub says Copilot reviews default to “Comment,” which does not satisfy required approvals; administrators can configure “Approve.” Re-reviews of later pushes require configuration or a manual request, and repeated comments can occur. [GitHub documentation](https://docs.github.com/en/copilot/how-tos/use-copilot-agents/request-a-code-review/use-code-review).
- **Vendor guidance, not measured effect (accessed 2026-09-24):** GitHub describes Copilot as a first-pass reviewer and places final approval with the human team. [GitHub product page](https://github.com/features/copilot/code-review).

### Inferences
- Use AI to propose specific failure scenarios, missing tests, and review targets. Require a reproducible oracle or accountable reviewer to close a card; no-comment output is only “no finding by this reviewer.”
- If AI both implements and reviews a change, require an independent behavioral check, since the same untested assumption can appear in both artifacts. This is a prudential rule, not a measured independence benefit from the studies above.
- Record each AI finding's disposition (confirmed, rejected with reason, unresolved) and keep unaddressed high-severity findings open.

### Gaps
- No cited study estimates false-negative rates for this repository's specific models, prompts, codebase, or card types. A local labeled evaluation is needed before assigning AI-review sensitivity.
- The observational studies measure responses to comments, not prevented incidents or whether replacing human approval is safe.

## Where do human review bottlenecks arise, and what work should reviewers prioritize?

### Takeaway
Human review is costly, and larger, more scattered changes reduce feedback usefulness. Reviewers should spend scarce attention on intent, user-visible behavior, architecture, and failure modes after machine-checkable details are handled by reproducible checks.

### Cited Findings
- **Measured, case study (Sadowski et al., ICSE 2018):** Google researchers used 12 interviews, a 44-person survey, and logs for 9 million reviewed changes to study motives, practice, and challenges in modern code review. The design describes Google and is not a controlled estimate that review prevents a fixed number of bugs. [Google Research](https://research.google/pubs/modern-code-review-a-case-study-at-google/).
- **Measured, mixed-method observational (Bosu, Greiler & Bird, May 2015):** Analysis of 1.5 million review comments across five Microsoft projects found that as the number of files in a change grows, the proportion of comments useful to the author falls. The authors also found reviewer experience correlated with usefulness, rising sharply in the first year and plateauing thereafter. [Microsoft Research](https://www.microsoft.com/en-us/research/publication/characteristics-of-useful-code-reviews-an-empirical-study-at-microsoft/).
- **Interpretive research argument, not a controlled effect (Bacchelli & Bird, 2013/2014):** Microsoft researchers argue that review has costs and that “find bugs” is an incomplete description of what practitioners use it for; the provocative title should not be read as evidence that reviews never find defects. [Microsoft Research](https://www.microsoft.com/en-us/research/publication/code-reviews-do-not-find-bugs-how-the-current-code-review-best-practice-slows-us-down/).

### Inferences
- Split a card's verification into a concise claim, artifact, and oracle. Route mechanical checks (build, type checking, invariant tests, changed-file checks) first, then present only residual questions to a human.
- Group cards sharing one build or interaction journey so evidence can be reused, but do not let a passing group check silently certify a distinct card claim.
- Escalate broad diffs and cross-component changes even if an AI reviewer is silent; Microsoft’s association is a warning about reviewability, not a universal line-count threshold.

### Gaps
- No robust published estimate here converts 550 heterogeneous cards into reviewer-hours; measure a pilot cohort's actual time and rework.
- The case studies do not identify a universal optimal change size or a guaranteed defect detection rate.

## How should risk-based QA and sampling allocate verification effort?

### Takeaway
Risk sorting can make limited testing earlier and more consequential, but risk scores and samples need calibration. A sample gives a statement about a defined population and error rate; it does not certify unsampled high-consequence cards individually.

### Cited Findings
- **Official guidance (NIST SP 800-30 Rev. 1, 2012-09-17):** Risk assessment considers threats, vulnerabilities, likelihood, impact, and response; its purpose is to inform decisions, including residual risk, rather than to replace decision makers. This security framework is a conceptual analogue for software QA, not a validated scoring formula for these cards. [NIST publication](https://csrc.nist.gov/pubs/sp/800/30/r1/final).
- **Measured, retrospective empirical software study (2017):** Comparing risk-coefficient and lines-of-code ordering across five Java products, the risk approach reached all known defects after testing 51.6% of classes on average versus 61.8% for size ordering. Size ordering led at some early points and in some products; the result concerns test prioritization, not safe closure of untested items. [Software Quality Journal study](https://link.springer.com/article/10.1007/s11219-016-9345-3).
- **Official statistical method (NIST handbook, accessed 2026-09-24):** An acceptance sampling plan specifies random sample size and an acceptance number; its operating-characteristic curve gives probability of accepting a lot at each actual defective fraction. Sampling must therefore name both the tolerated defect rate and chance of missing it. [NIST OC-curve chapter](https://www.itl.nist.gov/div898/handbook/pmc/section2/pmc243.htm).
- **Practitioner operations evidence (Google SRE Workbook, 2018):** Google describes canary rollout as limiting exposure while observing production metrics; its worked example contrasts a defective release affecting 20% of requests globally with a 5% canary that yields 1% overall errors under simplifying assumptions. This reduces blast radius; it does not prove the release has no latent failures. [Google SRE canary chapter](https://sre.google/workbook/canarying-releases/).

### Inferences
- For each card, score consequence (money, data loss, privacy/security, availability, user trust), blast radius, novelty, reversibility, and quality of its oracle. A high-consequence card needs individual evidence and an accountable decision regardless of its numerical score; risk ranking controls order and depth, not whether proof is required.
- If all 550 were a homogeneous random population and verification detected every defect it inspected, a zero-failure simple random sample of 59 has about 95% chance to include at least one defect when the true defect fraction is 5% (calculation: 1−0.95^59≈0.952). About 299 samples are needed for the same assurance at 1% (1−0.99^299≈0.950). These are binomial approximations to NIST's acceptance-sampling model; finite-population hypergeometric calculation is preferable for the actual 550. Neither figure licenses closing the unsampled cards, because card risks differ and verification itself may miss defects. [NIST OC-curve method](https://www.itl.nist.gov/div898/handbook/pmc/section2/pmc243.htm).
- Use stratified random audit only for a large, genuinely homogeneous low-risk class after each member passes its deterministic gate. Predeclare sample size, acceptance threshold, what constitutes a miss, and escalation: any material miss triggers investigation of the full class and revised gate. Store the random seed and sampling frame.
- Canary/rollback and monitoring are useful additional evidence for reversible deployments, not substitutes for pre-release review of high-impact behavior.

### Gaps
- The backlog's actual distribution of card types, risk, dependence, and test reliability is unknown, so no defensible blanket sample size or auto-close percentage can be inferred.
- The retrospective risk study's known-defect dataset may not represent failures in this application, and its ranking did not verify production consequence.

## What can be auto-closed, and where is human judgment indispensable?

### Takeaway
Auto-close only when the card's acceptance claim is fully decided by a reproducible, current, scoped oracle and the consequence of a false pass is low. Human judgment is needed when the oracle itself is subjective, the change has significant stakes, or evidence leaves a plausible untested failure path.

### Cited Findings
- **Experimental automation-bias evidence outside software (1999):** In a simulated flight task, participants using decision aid recommendations made omission errors when the aid failed to prompt and commission errors when it recommended an action despite contradictory valid indicators. This establishes a failure mode of human oversight around automation, not a measured rate for code review. [Original journal article](https://www.sciencedirect.com/science/article/pii/S1071581999902525).
- **Official product behavior (accessed 2026-09-24):** GitHub's default AI-review behavior avoids counting Copilot comments as required approval, though an administrator can change the setting. This is a governance design choice, not proof of performance. [GitHub documentation](https://docs.github.com/en/copilot/how-tos/use-copilot-agents/request-a-code-review/use-code-review).
- **Measured AI security limitations (2026):** The Copilot evaluation's missed vulnerability classes show why an AI “pass” cannot be the only security oracle. [Conference paper](https://proceedings.mlr.press/v318/amro26a.html).
- **Operational guidance (Google SRE, 2018):** Canarying requires metrics capable of detecting the relevant regression and a rollback path; low observed errors on a canary are limited by what traffic exercised and what was measured. [Google SRE canary chapter](https://sre.google/workbook/canarying-releases/).

### Inferences
- **Eligible for automatic closure, after an initial human check of the oracle:** Exact build/package production; compiler/type/lint errors; deterministic protocol or schema round trips; fixed regression tests with meaningful assertions; byte-for-byte or hash checks; reproducible documentation-link or generated-file checks. Require an explicit expected result, command, environment/version, artifact, timestamp, and pass output. A passing test closes only the behavior it actually asserts.
- **Needs human judgment:** UI appearance and usability; audio/video perception; accessibility experience beyond mechanical checks; vague or contested acceptance criteria; architecture and maintainability; privacy/security threat modeling; money, deletion, credentials, legal or clinical effects; performance claims without a calibrated workload; external service behavior; rare race or rollback paths; any card whose oracle was written solely by the implementing AI without independent validation.
- **Review presentation to reduce automation bias:** Show the original request and observed artifact before displaying AI's conclusion; ask the verifier to record a short independent verdict and then reconcile AI findings. This is an intervention inferred from the flight-task mechanism, not a code-review trial result.
- **Decision matrix:** (1) failing or stale deterministic check → keep open; (2) passing complete low-risk deterministic oracle → auto-close with receipt; (3) passing but partial oracle → targeted human or independent probe; (4) human-facing or high-stakes claim → accountable human verdict; (5) no executable oracle → leave explicitly deferred or request a decision, never mark verified solely because an AI reviewer found nothing.

### Gaps
- There is no primary evidence establishing that any specific fraction of this backlog is safely auto-closable without reading the cards and validating their oracles.
- The aviation automation-bias experiment may not transfer quantitatively to software review; a small local blinded audit could test whether showing AI verdicts first changes verifier error rates.
