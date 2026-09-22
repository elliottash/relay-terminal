# Calibrating AI bug finding for Relay

Research for #SJTR, 2026-09-21. Discussion only: no implementation, benchmark run, card edit, or commit. Primary sources were opened during this research. Local references describe the shared working tree as read, not a frozen release. The parent owns landing this file. `relay_board` tools were not visible in this research harness; no delegation was attempted.

## 1. What evidence would establish that an attack agent finds useful defects?

### Takeaway

Use separate evidence for detecting seeded faults, rediscovering historical real faults, and producing useful findings on ordinary work. A benchmark of known buggy cases cannot establish the precision or interruption cost users will experience in a mostly healthy project.

### Cited Findings

- **Verified research:** Just et al. studied 357 real faults across five applications and found mutant detection correlated with real-fault detection after accounting for coverage. This supports mutation as a useful proxy in that setting, not equivalence between artificial faults and all real defects, and not validation of an LLM reviewer. [Authors' FSE 2014 abstract](https://homes.cs.washington.edu/~mernst/pubs/mutation-effectiveness-fse2014-abstract.html).
- **Verified benchmark design:** Defects4J selects reported, single-commit, source-code fixes, prunes unrelated edits, and requires a stable test failing before and passing after the fix. It excludes broken/flaky tests and documents runtime and timezone dependencies. These are valuable reproducibility practices, but deliberately selected defects do not represent all production failures. [Defects4J, “The bugs” and “Reproducibility”](https://github.com/rjust/defects4j#the-bugs).
- **Verified benchmark scope:** BugsInPy's paper describes 493 real bugs across 17 Python programs. It offers a closer language match for Relay's Python work than Java-only evaluation, but does not establish generalization to Relay. [BugsInPy paper](https://arxiv.org/abs/2401.15481).
- **Verified benchmark design:** Magma ports historical bugs into contemporary library versions and instruments whether execution reaches buggy code and whether inputs actually trigger the fault. Its projects and harnesses are selected; reaching a line is not triggering a defect. [Magma authors' project](https://hexhive.epfl.ch/magma/).
- **Verified task distinction:** SWE-bench supplies an issue description and asks the model to edit a repository to resolve it; the original dataset contains 2,294 issues from 12 Python repositories. This is informed repair, not discovering an unknown bug in an arbitrary change. [SWE-bench paper](https://arxiv.org/abs/2310.06770).
- **Verified closer task:** SWT-Bench evaluates generated bug-reproduction tests and separately reports issue reproduction and coverage of the resolving patch. Its site also records grading changes, illustrating why harness version belongs in evaluation provenance. Neither a reproduction score nor patch coverage is an open-ended bug-finding precision score. [SWT-Bench authors' site](https://swtbench.com/).
- **Verified agent-evaluation finding:** Kapoor et al. identify accuracy-only optimization, inadequate holdouts, and reproducibility problems in agent evaluations; they advocate evaluating cost alongside accuracy and matching holdouts to intended generalization. [AI Agents That Matter](https://arxiv.org/html/2407.01502v1).
- **Verified local context:** #SJTR already proposes planted and externally documented defects, advisory operation until validity is measured, and a separate findings section. The earlier skeptical review explicitly says fifteen planted defects plus fifteen clean commits do not justify general authority. [Card:44](</home/elliott/repos/relay-terminal/issues/features/2026-09-21-tests-becomes-the-front-door-to-a-qa-attack-system.md:44>), [card:56](</home/elliott/repos/relay-terminal/issues/features/2026-09-21-tests-becomes-the-front-door-to-a-qa-attack-system.md:56>), [skeptical review:67](</home/elliott/repos/relay-terminal/docs/research/qa-across-fields/f-codex-skeptical-review.md:67>).

### Inferences

**Recommended evaluation corpus, with separate reported strata:**

| Stratum | Purpose | Main limitation |
|---|---|---|
| Independently planted faults / mutation | Check harness sensitivity and specific failure classes | Artificial cues, equivalent mutants, chosen operators, injector's assumptions |
| Historical pre-fix revisions | Rediscovery of real failures with external provenance | Public memorization; survivorship of reported, fixable, reproducible defects |
| Paired repaired controls | Test whether the same allegation disappears after its cause is removed | The repaired revision can contain unrelated bugs; it is not globally “clean” |
| Ordinary consecutive changes in shadow | Measure actual report usefulness and burden | Unknown total defect count; cannot honestly estimate absolute recall |

These are design recommendations inferred from the benchmark selection rules above. Curators should choose cases before trying candidate agents, record every exclusion and why, and sample across Python/backend, C++/terminal behavior, UI state, configuration, and concurrency only to the extent each class has a credible oracle. Do not quietly exclude inconvenient environment failures: report both eligible-case performance and operational completion over all selected cases.

For historical cases, remove the fixing commit, future history, issue discussion, added regression tests, revealing filenames, and answer annotations from the agent-visible snapshot. Keep original provenance and the answer key in an evaluator-only bundle. A supplied issue description changes the task to reproduction; benchmark that as a separate mode. A public case remains potentially memorized even after metadata is removed.

Use explicit units; the following are proposed operational definitions, not empirical findings:

| Measure | Denominator and interpretation |
|---|---|
| Finding precision | Adjudicated true unique findings / all adjudicated true-or-false unique findings |
| Unsupported-report share | False unique findings / adjudicated unique findings; this equals 1 − precision, **not** classical false-positive rate |
| Case-level false-alarm rate | Known-negative cases receiving ≥1 false allegation / known-negative cases; define the negative scope in advance |
| Known-defect recall | Distinct catalogued defects discovered / eligible catalogued defects, separately for planted and historical strata |
| Operational yield | Confirmed novel actionable defects / all attempted changes or campaigns, including incomplete runs |
| Replay success | Replays reproducing the claimed behavior / attempted replays; distinguish expected assertion failure, crash, timeout, and setup failure |
| Human cost | Total review, reproduction, adjudication, duplicate handling, and appeal minutes; also median/p90 per finding and total minutes per confirmed actionable defect |
| Incremental value | Additional confirmed defects beyond existing checks or a matched baseline, at equal compute and/or human budget |
| Coverage of evaluation | Completed / attempted runs, unresolved / all findings, eligible / selected cases, and reasons for exclusion |

Do not invent a true-negative denominator by counting every non-reported line of code. Open-ended discovery has no natural inventory of all possible allegations. For controls, distinguish “the known historical defect is absent” from “no valid defect of any kind exists.” An unexpected real bug found in a control is a discovery, not an automatic false positive; adjudicate, update the label, and disclose the change.

Keep unresolved findings visible. Report precision bounds with unresolved reports treated first as false and then as true, alongside adjudicated precision. Count duplicates separately as burden even after deduplicating the precision numerator. Separate factual validity, severity, novelty, and actionability: a real but already-known low-impact defect is not a false positive, yet may add little value.

**Illustrative calculation, not an observed rate:** with 1% defective changes, 80% sensitivity and a 5% case false-positive rate, a binary detector's positive predictive value is `0.008 / (0.008 + 0.0495) ≈ 14%`. This Bayes calculation assumes one binary decision per change and does not directly describe multi-finding reports. It demonstrates why a balanced planted-bug dataset cannot predict operational precision.

### Gaps

- No reviewed source establishes Relay's defect prevalence, expected precision, human cost, or optimal thresholds. Those require a local pilot.
- No finite ordinary-work sample proves “clean”; external review can improve labels but cannot supply complete recall ground truth.
- Broad scientific-analysis or UX quality claims need their own oracles and samples. A successful code-defect pilot would not validate those domains.

## 2. What makes a finding independently verifiable and reproducible?

### Takeaway

A second model's agreement is supporting evidence, not an oracle. Prefer independently specified behavior, executable counterexamples, and clean replay; measure the marginal benefit of model diversity rather than assuming provider labels establish independence.

### Cited Findings

- **Verified research:** Kim et al. evaluate more than 350 LLMs and find correlated errors, including among models from different providers and architectures. Their tasks include leaderboard questions, judging, and resume screening, not Relay bug discovery. Thus this is evidence against assuming independence, not an estimate of coding-review diversity benefits. [ICML 2025, Correlated Errors in Large Language Models](https://proceedings.mlr.press/v267/kim25e.html).
- **Verified research:** SWE-agent studies how the agent-computer interface affects software-engineering performance. The model is only one part of the evaluated system. [SWE-agent paper](https://arxiv.org/abs/2405.15793).
- **Verified method:** Delta debugging reduces failure-inducing input or changes using a predicate that checks whether the failure persists. A smaller counterexample can reduce the evidence a reviewer must inspect; the method depends on the predicate being appropriate. [Zeller's research project and papers](https://www.st.cs.uni-saarland.de/dd/).
- **Verified local discrepancy:** #SJTR describes a different-family attacker as a rule; current board policy explicitly calls a different-family verifier a recommendation, not a rule. The prior synthesis's stronger “minimum viable” wording should not be treated as demonstrated bug-finding evidence. [Card:38](</home/elliott/repos/relay-terminal/issues/features/2026-09-21-tests-becomes-the-front-door-to-a-qa-attack-system.md:38>), [policy:174](</home/elliott/repos/relay-terminal/issues/POLICY.md:174>), [synthesis:70](</home/elliott/repos/relay-terminal/docs/QA-ACROSS-FIELDS-RESEARCH.md:70>).

### Inferences

**Recommended oracle hierarchy:** an independently written requirement/invariant; a trusted reference or differential relation with stated limits; a reproducible crash/sanitizer violation; a human domain judgment; and, last, a model hypothesis awaiting confirmation. Differential disagreement alone does not prove which side is wrong. A metamorphic relation can itself be mistaken. A crash proves a failure under the observed input and environment, not necessarily exploitability or user impact.

Curate the expected behavior before exposing a case to candidates. Have a reviewer who did not inject the defect or author the finding adjudicate the evidence, blinded to candidate identity where practical. Use a second adjudicator for disputes and a prespecified sample of accepted and rejected findings. Report disagreement and unresolved cases rather than manufacturing consensus. Check claimed file locations and quotations against the pinned revision; citation correctness is separate from whether the alleged behavior is wrong.

Each finding should carry: checked source hash; dirty-state or snapshot manifest; scope; model/version; prompt and harness version; permissions; time/token/tool budget; environment/dependency versions; random seed where supported; exact input or interaction sequence; expected behavior with oracle provenance; observed behavior; full relevant logs; minimized reproducer; repeat count and outcomes; and the deduplication/root-cause key. Logs may need a redacted export distinct from private raw evidence.

For historical evaluation, run a proposed reproducer on both pre-fix and fixed revisions under identical setup. Verify that it fails for the intended reason on the first and passes on the second, without reading a version tag or testing for a patch's textual presence. Do not let the attacker edit the evaluator or oracle. A failing test authored by the same model is a candidate counterexample until its assertion is reviewed.

Minimize inputs or GUI action sequences only while preserving the claimed failure predicate. Save the original and reduced witnesses. Race failures require repeated or schedule-controlled trials; a failed replay is “not reproduced,” not proof that the allegation is false. Record tool/build failures as infrastructure outcomes, not application defects.

Freeze prompt, tool interface, model version, and budget before the held-out phase. Split by defect/root-cause family and time or subsystem where possible, keeping a buggy/fixed pair in the same split. No tuning on hidden results; a changed configuration starts a new evaluation with fresh held-out cases. Model providers may already have trained on public fixes, so use fresh private or newly curated cases for stronger evidence, without claiming contamination can be ruled out completely.

If diversity is evaluated, compare (A) one pass, (B) a fresh second pass from the same family, and (C) a fresh pass from another family at matched total budget. Keep the second pass blind to the first report initially. Measure additional true defects, additional false reports, overlap of misses, adjudication time, and cost. This separates more sampling from family diversity; no generic “independence score” is warranted by the cited evidence.

### Gaps

- This research found no primary result establishing a universal optimal model pairing for code review, or demonstrating that a different provider makes a bug verdict reliable enough to gate a release.
- GUI preference, unspecified intent, and some concurrent failures will remain human-judged or inconclusive. A reproducible undesirable behavior can still require an owner decision about the intended behavior.

## 3. What bounded pilot should Relay discuss, and how should it stop?

### Takeaway

Start with an opt-in, advisory code-review pilot over immutable snapshots, with controlled replay for promising findings. Keep test failures and exploratory findings visibly separate; promotion to gating should require a later, narrowly scoped decision supported by fresh evidence.

### Cited Findings

- **Verified local design:** #SJTR preserves Tests and proposes an additional findings section with “Make a card,” advisory until measured. The broader synthesis distinguishes “no findings” from “not reviewed” and requires recording time limits and environment differences. [Card:56](</home/elliott/repos/relay-terminal/issues/features/2026-09-21-tests-becomes-the-front-door-to-a-qa-attack-system.md:56>), [synthesis:80](</home/elliott/repos/relay-terminal/docs/QA-ACROSS-FIELDS-RESEARCH.md:80>).
- **Verified local implementation, narrow claim:** `Isolation.h` describes per-pane systemd scopes and memory containment, allows an unavailable/unknown probe to run unisolated, and wraps commands in a user scope. This file is not evidence of filesystem, credential, or network containment for malicious test execution. [Isolation.h:20](</home/elliott/repos/relay-terminal/src/Isolation.h:20>), [Isolation.h:35](</home/elliott/repos/relay-terminal/src/Isolation.h:35>), [Isolation.h:164](</home/elliott/repos/relay-terminal/src/Isolation.h:164>).
- **Verified security benchmark design:** CVE-Bench evaluates exploitation of historical real web-application vulnerabilities in a sandbox framework. It supports testing concrete exploit outcomes in controlled environments; it does not certify Relay's containment or measure all security defect classes. [CVE-Bench paper](https://arxiv.org/abs/2503.17332).

### Inferences

**Proposed pilot for discussion; all counts and limits below are design choices, not established thresholds.**

1. **Scope and curation:** choose one well-oracled area first, such as Python state/protocol logic or terminal parser behavior. Prepare 10 development cases for prompt/harness debugging. Then seal 40 evaluation cases: 10 independently planted defects, 10 historical real defects, and 20 paired repaired controls. Keep pairs together and do not expose labels. Curators independently verify that each selected defect is observable in the pinned environment. Stop curation if credible cases cannot be obtained within a pre-agreed effort budget rather than silently substituting easy examples.
2. **Bounded execution:** one frozen candidate configuration, at most 10 minutes per case, and a pre-agreed total inference/spend cap. Maximum 400 agent-minutes for the 40-case evaluation, excluding explicitly bounded setup. This small run is feasibility evidence, not certification. Use existing checks as baseline and record whether the agent adds anything beyond them. Only compare an additional configuration if the agreed budget can support a matched comparison.
3. **Operational shadow:** after mechanical validation, review the next 20 eligible consecutive changes or run for two weeks, whichever comes first. Do not choose only suspicious changes; record eligibility rules, skipped changes, and incomplete runs. Existing development proceeds without agent blocking. Budget, for example, four total human triage hours; reaching that cap stops intake and preserves the untriaged remainder as unresolved.
4. **Review outcomes:** report raw numerators/denominators by stratum and defect severity, uncertainty intervals, replay outcomes, novel actionable yield, duplicate and unresolved rates, wall time, inference cost, and human minutes. Human cost must include false reports and setup, not only accepted findings. Cluster uncertainty by defect/pair when repeated runs or several reports share one cause; repeated seeds are not independent new bugs.
5. **Decision meeting:** compare operational value with the baseline and discuss whether to continue advisory use, revise the configuration, or stop. Do not automatically promote after these 40+20 cases. If an owner wants an operational target—such as a maximum nuisance-report rate or maximum minutes per useful defect—write it before looking at held-out outcomes and label it a product tolerance.

**Stopping and escalation rules, proposed:**

- Stop a campaign immediately for writes outside the disposable target, credential exposure, unauthorized external traffic, or inability to enforce its declared boundaries. Preserve diagnostic evidence privately and label the run invalid/incomplete; do not let it count as a clean result.
- Stop intake at the time, spend, storage, or human-triage cap; cancellation must stop child processes as well as the agent. Report censored and unreviewed work.
- Invalidate the affected held-out phase after answer leakage, oracle modification, incorrect snapshots, or tuning on evaluation results. Fix the harness on development cases and use a fresh holdout.
- Predeclare a futility review at the end of the operational window. Zero confirmed incremental actionable findings, a triage backlog exhausting the cap, or cost exceeding the owner's declared tolerance are reasons to stop or redesign, not proof that AI bug finding never works.
- An independently confirmed severe defect merits prompt attention under existing project practices, but one dramatic success does not waive the rest of calibration.

**Uncertainty illustration:** with zero false alarms in 20 independent genuinely negative cases, the one-sided exact 95% upper bound on the case false-alarm probability is `1 − 0.05^(1/20) ≈ 13.9%`. Zero in 15 permits about 18.1%; zero in 60 permits about 4.9%. These are binomial calculations under stated independence and labeling assumptions, not observed results or recommended acceptance standards. Paired cases and uncertain “clean” labels weaken such interpretations. Avoid repeated significance peeking or stopping merely because an attractive score appears.

**Suggested UX:** preserve existing test-run results and add a separate “Findings” list. Show “candidate,” “reproduced,” “confirmed,” “dismissed,” “duplicate,” and “inconclusive” as evidence states, not a single red/green project verdict. Show scope, revision, last run, completion status, and exhausted budget. “No findings in this 10-minute review” must not read as “project is safe.” A confirmed reproducer can become an ordinary regression test after review; a candidate finding should not silently enter the existing failing-test signal/automatic-fixer lifecycle. Provide replay, inspect evidence, dismiss with reason, and make/link card actions. Keep severity distinct from certainty.

**Suggested security boundary:** use a disposable source export and scratch build area, synthetic data, isolated application state, no host credentials or agent sockets, no shared writable checkout, and no external network by default. Keep inference credentials in the controller outside executable tests; model access may need a narrow mediated channel. Contain CPU, RAM, process count, disk, wall time, and network; running locally alone is not isolation. The pilot must fail closed if its required containment is absent, unlike an ordinary terminal's optional memory scope. A container without a carefully restricted host interface should not be described as a complete security boundary. Use a stronger VM boundary where the threat model includes hostile native code or exploits. Treat repository text, logs, and findings as untrusted input and avoid making their instructions authoritative for the controller. Restrict security experiments to the disposable target, not users' real accounts or services.

### Gaps

- No pilot has been executed here. The proposed case counts, times, cost tolerances, and promotion process remain decisions for the owner.
- The repository read confirms memory-scope behavior, not a complete security audit. A future execution design must establish and test its actual boundaries before adversarial workloads run.
- A low-volume pilot cannot establish rare-event safety or justify universal release gates. Any future gate should name its narrow defect class, independent oracle, validation sample, rollback behavior, and recalibration trigger.
