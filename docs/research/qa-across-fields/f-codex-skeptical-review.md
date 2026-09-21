# Codex (gpt-6-astra, reasoning high): a skeptical second opinion on the QA-support plan — 2026-09-21

Asked for by the owner ("query codex cli to give you a skeptical take and to see if this is forced"). The prompt carried docs/QA-ACROSS-FIELDS-RESEARCH.md, cards #YZ8G and #7BM4, the #7BM4 human-QA brief and scenario, board_policy.md and SWITCHBOARD-DESIGN §4.14 pasted in full. Verbatim.

**Verdict: yes, the per-card taxonomy is forced.** The useful product is “prepare the evidence and put the unresolved question in front of me.” The proposal wraps that in classification, duplicated paperwork and gates whose confidence exceeds their evidence. “Build through phase 3” is too much as currently written: phase 2 already contains a speculative usage-attribution system.

**A. Keep the actions; drop the card types.**

A developer recognises tests, comparisons and reproductions. An analyst recognises reconciliation, sensitivity analysis and reviewing results. A researcher recognises reproduction, source checking and methodological criticism. None needs their work assigned one of five mutually exclusive words.

The categories mix different dimensions: **measure** describes evidence, **read** describes reviewer activity, **scenario** describes setup. One estimator change can require all five: check invariants, compare outputs, measure bias, exercise a problematic dataset and read whether the assumptions justify the conclusion. #7BM4 itself spans checks, an interactive scenario and profiling. Which chip represents it?

Cut the mandatory chip and its override. Keep optional actions with concrete names: **Run checks**, **Compare results**, **Try it**, **Review**. Measurement belongs wherever a numerical claim needs evidence. Rename “read” to “Review”; reading is not an acceptance criterion. Let an agent propose the next useful action without classifying the whole card.

**B. The first clunk is already in the worked example.**

| Resented surface | Specific problem | Lighter alternative |
|---|---|---|
| **HUMAN-QA.md setup** | Two commands, a temporary binary, a startup-crash exception, missing wording fixes, second-instance caveats and window-width instructions before reviewing anything. | One **Try it** action launches the pinned build and disposable fixture. If preparation fails, report that before requesting review. |
| **Human-only steps** | Running tests, filtering `is:slow`, opening a flame graph and attaching evidence are marked `human`. Those are largely mechanical operations the promised AI-first pass did not finish. | Automate them. Ask the person to perform only the task whose usability or interpretation needs observation. |
| **Scripted expectations** | Scenario 2 tells the reviewer that `inventory_sync` is the flaky test, then asks whether they could identify it within a minute. That measures following instructions. | Give the problem without the answer. Observe whether they find it; reveal the expected result afterwards. |
| **Check → Run these → Check** | A person coordinates a state machine the application already understands. | Update evidence status automatically after runs; offer one action to run missing checks. |
| **Dated Check blocks** | Up to one block per hour accumulates machine output inside the work narrative. | Show the latest status; retain history behind a link and freeze the relevant evidence at acceptance. |
| **Sections and verdict entry** | Tests, QA checklist, Human QA, Verdict, Resolution, evidence files and thread comments overlap. The brief asks for observations in tables and answers on the card. | Record each result once and render it where needed. Keep the owner’s mechanical/human distinction without requiring duplicate entry. |
| **Shape chip** | A wrong classification becomes another thing to correct before doing the work. | No chip unless users demonstrate a need to filter by it. |

“Never the same question twice” is also the wrong rule. Changed behaviour can require another judgment. The rule should be: **do not ask again while the relevant evidence and result remain applicable.**

**C. The gate is defensible only as enforcement of an explicit acceptance requirement. As written, it will produce routine overrides.**

“Never run here” is not “never run.” The appendix explicitly anticipates nearly all 4,550 tests appearing unrun locally. An accepted CI result should satisfy the requirement when it identifies the relevant revision and environment.

Conversely, “passed last time” is not proof about this change. Hashing the test source does not establish freshness when the implementation, dependencies or inputs changed. Recording commit and dirty-tree identity helps only if the gate actually checks applicability.

“Gone” could mean a renamed or deliberately retired test. A flaky failure could be unrelated. These conditions need resolution, but they are not interchangeable evidence of unfinished work.

Make the status distinguish **passed, failed, missing evidence and not applicable**. Block acceptance for explicitly required checks with applicable failures or missing evidence. Offer “Run,” “Use this existing result,” or “Replace retired check.” Keep incidental historical findings advisory. Apply the same acceptance decision through GUI and agent tools.

An override becomes a rubber stamp when the same known flake, unsupported runner or local-history gap prompts it repeatedly. Record an exception scoped to the check, relevant environment and revision or expiry; invalidate it when those conditions change.

The current escape hatch is especially bad: §4.14 says a card without `## Tests` is ungated. That rewards omitting evidence. Required acceptance checks must come from the agreed task, not merely whatever list happens to be present.

**D. “Proven in use” is false reassurance under that name. Call it “Observed use.”**

The owner’s recorded decision says **propose closure**, not silently close. Preserve that distinction.

File overlap cannot establish feature exposure. Opening the Switchboard does not exercise its refusal path. Exercising the refusal path does not show that it refuses the right work. Crashes and worker faults miss wrong numbers, misleading reports, frustrating interactions and users abandoning a task.

Before usage can support closure, require:

- A named behaviour and relevant input conditions, observed on the changed version.
- A defined success signal and credible detection of the failure being claimed absent.
- Separation of seeded demonstrations, automated repetitions and actual use.
- A threshold justified for that failure mode; repetition alone adds little.
- Explicit treatment of unresolved acceptance questions.

Without that, show “used 43 times over eight days; no captured faults” and stop there.

For a research result, “cited-and-unchanged” is not validation. It can mean an error has propagated. Delete that proposed translation.

**E. Do not build through phase 3 as written. Build a much smaller slice, then use it.**

1. **Phase 1:** retain evidence capture, but fix gate semantics and duplicated records.
2. **Phase 2:** build an explicit verification record identifying the checked revision, evidence and unresolved issues. Use a separate verification session. Skip the chip and usage-based closure.
3. **Phase 3:** build **Try it**, with reproducible setup, a completed mechanical pass and a short human task. Do not restrict it to cards carrying `scenario`.
4. **Phase 4:** move a minimal comparison view earlier. Reviewing changed output serves UI work, backend transformations and analysis immediately. “Evidence without a verdict” contradicts the promised automatic “two-second yes or no”; allow uncertain and unexpected results.
5. **Phase 5:** evaluate immediately, but do not treat fifteen planted defects and fifteen clean commits as a licence for general authority. That is a pilot covering chosen examples. Assess missed defects, false alarms, unjustified passes and reviewer effort on representative work.
6. **Phase 6:** defer risk machinery and additional states until concrete users require them. Never build mandatory five-type routing or universal closure from quiet logs.

Resolve the contradictory rules first. Decisions permit agent movement and require a separate session; the Plan demands a different model family, and phase 5 forbids verifier movement before calibration. Policy requires the implementer to supply a checklist; the proposal forbids it. Policy says invent no headings, yet omits `Human QA` and `Profile`.

Also missing: acceptance expectations recorded **before implementation**. The verifier should report what it independently checked against those expectations. Writing the entire checklist afterwards invites selecting criteria the implementation happens to satisfy.

**F. The central mistake is confusing organising verification with establishing that the work achieved its purpose.**

A chip, fresh session, completed checklist and quiet logs can all coexist with solving the wrong problem. Even “a subsystem needs no person at all” assumes someone already chose an adequate contract.

The simplest common design is:

- The card states the intended outcome and how failure would be recognised.
- The agent prepares the changed artifact and applicable evidence.
- Relay shows what passed, what failed and what remains unanswered.
- When judgment is needed, Relay opens the relevant thing and asks one specific question.
- Acceptance records the evidence and decision for that revision.

For an app, that thing is a staged interaction; for a backend, a request and response or failure reproduction; for an analyst, changed rows and reconciled totals; for a researcher, a figure, its reproduction and the claim it supports.

Those are different things to inspect, not different kinds of card. Build the preparation and handoff. Drop the classification machinery.
