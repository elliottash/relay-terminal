---
id: 1QKM
type: work
status: discussing
labels: [feature, switchboard, board, qa, skills]
waiting_on: owner
priority: 2
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [reports/Knowledge work across projects.md, research_notes/Knowledge work across projects/], related: [P2W8, BX7B, SJTR, R246, YZ8G, MEPR, 3KB7], github: null}
---
# Cards build servers, servers serve cases: skills as the Switchboard's second object, across knowledge work

## Issue
i want to broaden this discussion. consider these points, also related to #P2W8. first, i want to think about the following cases of analysis for knowledge work, as a broad set of motivating examples: app developer; ML / data science researcher; (social) scientist doing exploratory analysis with existing dataset; (social) scientist designing and implementing controlled experiments; mathematician / formal theorist; scientist reviewing a paper; scientist doing lit review; teacher preparing course materials; admin assistant dealing with admin tasks; game development. what other types of knowledge work need flexible, powerful, and intuitive AI support?

[...] it speaks to "skills" as a potential corollary to "cards" in AI supported knowledge work. so skill development and maintenance could be an important new component we need in the switchboard. lets start thinking about how to map out the dimension between skills and cards, and whether there are other pivotal dimensions that will call out for other tooling.

[...] the codification axis is missing that in software, cards build a non-AI tool that will deliver a task repeatedly. skills are a system where AI delivers tasks repeatedly. a card is built, a case is served. a program serves the case algorithmically; a skill serves it intelligently.

[...] 2 definitely yes, think about a doctor or lawyer using relay to help with their case work. write the first draft of the discussion card and then i have some more thoughts on it

## Discussion points
First draft, 2026-09-23. Relay is not only a software development tool; the Switchboard and the objects it manages should reflect the range of knowledge work it supports. This card records the model that came out of the conversation and the questions it opens. Nothing here is approved scope.

### 1. The core loop: cards build servers, servers serve cases

```
card  ──builds──▶  server  ──serves──▶  case
                   ├ program  (algorithmic)
                   └ skill    (intelligent)
```

- A **card** is the unit of *building*: a one-time piece of work, verified once, then done. It is capital.
- A **server** is what a card produces. A **program** serves a case by algorithm: deterministic, near-zero marginal cost, rigid. A **skill** serves it by reasoning: positive marginal cost (tokens, time, risk), flexible, needing judgement per case. Most real servers are mixed: `scripts/land.py` is a program a skill wraps; `jle-editing` is a skill whose steps are Playwright scripts with intelligent joints between them.
- A **case** is the unit of *serving*: one instance. "Referee this paper." "Pay September's rent." "Land this commit." It is flow.
- A case can also be served by a **person**: a doctor's patient, a lawyer's matter, a referee report written by hand. The case record holds person-, program- and skill-served cases alike (owner, 2026-09-23). That is what makes it the record of what to build next: the third ad-hoc case of the same shape is the signal to file a card that builds a server for it.

What this fixes:

- **The Board conflates building and serving.** Today every case becomes a card, so routine work (the twentieth rent payment) looks like project work. Cases need their own record, which server served it, at which version, outcome, cost, verdict, and are not cards. A card gets filed when a case fails and the server needs work.
- **Programs and skills fail differently, so their QA differs.** A program fails loudly (a crash, a red test) and is verified at build time, once. A skill fails plausibly (confident and wrong) and has to be verified *per case* until it has earned authority, after which it can be spot-checked by sampling. The "does this need human QA?" designation from #BX7B therefore mostly belongs on the **server** and is inherited by its cases, with the sampling rate falling as the track record grows. #SJTR's attack system is the same idea applied adversarially: attack the server, not the case.
- **The case ledger is the evaluation data.** Skill evals, verifier calibration (#SJTR's earned authority), the false-positive rate, the per-task effort suggestion: all of it is computed from cases. Without a case record the measurement the QA cards ask for has nothing to measure.

### 2. The codification axis lives inside the server

The meaningful axis is the **ratio of algorithmic to intelligent steps within a server**, and the boundary moves both ways:

- **Harden:** a skill step that has been identical across the last N cases is a candidate to become code. Cheaper, deterministic, testable.
- **Soften:** a program that keeps rejecting cases it cannot classify gets an intelligent step inserted at that joint.

The decision is economic and the agent can compute it per step: codify when case volume × marginal cost of the intelligent step exceeds the hardening cost, plus the reliability gain. `#MEPR`'s task plugins are the executable end of this axis.

A server is also the natural place to *declare* per-task defaults: verification mode (test script / AI on text output / AI on visual or multimodal output / a metric or level / pairwise rather than pointwise), default effort level, and whether human sign-off is required by rule. The agent's per-case suggestion is then "this case matches server X, whose default is Y", not a fresh guess each time. Whether a case runs automatically or waits for a person is a **policy** on the ledger, settable per project or globally ("cases of skill X in project Y run without review once 30 consecutive cases have passed").

### 3. Motivating cases of knowledge work

The owner's set, with the owner's own workspaces as examples: app developer (`relay-terminal`, `tracelaw`); ML / data-science researcher (`soundmatch`); social scientist doing exploratory analysis on an existing dataset and one designing controlled experiments (`modalities`, `AIComm`, `outcome_test`); mathematician / formal theorist (`theory_checks`); scientist refereeing a paper (`Referee-Work`); scientist doing a literature review; teacher preparing course materials (`ai_econ_summer_school`); admin assistant (`web-sites`, `hetzner`); game developer (`wondernauts`, `sweet-street`).

Additions, grouped by the axis each one adds rather than by domain:

- **The world, not the machine, is the oracle.** Bench / wet-lab scientist (the agent cannot run the experiment; a card waits on external evidence). Clinician or engineer signing off on something physical (checks are scriptable, but human sign-off is required by rule).
- **The artifact is a batch and the judgement is comparative.** Journal editor, programme committee, grant panel, hiring: pairwise and ranked comparison is the natural mode. Distinct from the referee, who judges one paper pointwise. The owner's `jle-editing`, `conference-paper-eval` and `editing-suggest-referees` skills are this case.
- **Verification is traceability, not correctness.** Lawyer / legal analyst (citations check against sources; the argument does not), investigative journalist / fact-checker / historian, compliance / auditor (attestation is the human step).
- **The artifact is a number that must reconcile.** Financial analyst, accountant, grant budget: totals reconcile by script, assumptions need judgement. #BX7B already names this shape.
- **The artifact is qualitative data.** Interview coding, content analysis: inter-rater reliability is a real metric, which makes it a clean example of verification *levels* (κ is checkable, the codebook's meaning is not).
- **The artifact is perceptual.** Visual / UI designer, illustrator, audio and video producer, architect / CAD: lowest verification level, pairwise preference is the only honest evaluation. #P2W8's typed previews (canvas, CAD with explicit Regenerate) anticipate it.
- **The deliverable is a running system.** SRE / sysadmin / DevOps: verification is a probe, rollbacks matter more than tests. Data / analytics engineer: schema and freshness tests, then a person reads the chart.
- **The goal is a person's understanding.** Student / self-learner (the inverse of the teacher; no artifact check confirms it). Translator / localizer (glossary consistency is checkable, register is judgement).
- **The work is coordination, not production.** PI / manager running a team (the Board itself), open-source maintainer (mostly judging others' changes), product manager / policy analyst / consultant.

These need not be enumerated exhaustively if the per-case suggestion is chosen along a few dimensions rather than by domain: (1) can the agent execute the artifact; (2) what is the oracle: a test, a reference, a source document, a reconciling total, a probe, a metric with a threshold, a human preference, the world; (3) one artifact or a batch, pointwise or comparative; (4) is human sign-off required by rule or by judgement; (5) perceptual or textual. The `blinded-model-review` skill (several models judge the same evidence blind, verdicts compared) is the pairwise/blind mechanism already in hand, waiting to be generalised. §7 turns these into the task profile.

### 4. Other objects with the same shape

The test for "needs its own tooling": durable, has a lifecycle, shared across cards, and things go wrong when it stays implicit.

| Object | Answers | Surface today |
|---|---|---|
| Cards | what is being built, is it done | Board |
| Servers (programs, skills) | how do we do this kind of thing | Tests covers programs; skills have no registry (see §5) |
| Cases | what was served, by whom, how judged, at what cost | none; Activity records turns, not cases |
| Workspace / context | where, with what: files, layout, host, kernel (#P2W8, #MEPR) | sessions, layouts; a card does not know its workspace |
| Artifacts | what came out: paper revision, trained model, dataset version, letter | git for code; nothing for models, data, figures |
| Evidence / verdicts | how do we know it is good (#BX7B, #SJTR, #YZ8G) | links on cards; not queryable across cards, so calibration cannot run |
| Knowledge / memory | what we know: user facts, project facts, bibliographies, contacts | Relay memory, `orgus`; nothing per project |
| Policy | who may do what, what must a person do, budgets, automatic vs manual (#3KB7) | `POLICY.md`, Options; no per-project layer |
| Schedules / triggers | when work recurs | cron, `/loop`; not shown as a set, not tied to cards |
| People | external parties: referees, students, clients, patients | none; cards have no counterparty field |

Not seven new buttons. The data model should name these objects so cards, servers and cases can point at them.

### 5. Skills as the first concrete child

The owner's own catalogue shows the maintenance problem: about sixty skills from three sources (`~/.claude/skills/synced`, `~/Dropbox/_Agents/.warp/skills`, `relay_core/skills_bundled`), duplicates (`create-skill` / `skill-creator`, `docs` twice), and a large share depending on external UIs (Editorial Manager, Beam, Amazon) that rot silently until a case fails.

A **Skills** surface in the Switchboard would hold, per server: version and provenance; last verified date; declared verification mode, effort default and human-QA rule; case history and pass rate; a stale flag. It closes the loop the Board cannot today:

1. **Case → card.** Repeated ad-hoc cases of one shape prompt "build a server for this".
2. **Server → card.** A case that fails because the server is wrong files (or suggests) a card on the server, not on the case that tripped over it.
3. **Server eval = shadow cases.** A server earns authority by passing on held-out cases and loses it when its failure rate on real cases rises, the same rule #SJTR sets for verifiers.

### 6. Evidence from the owner's own projects (2026-09-23)

Twelve of the owner's project trees were read (not run) and given one dossier each; the synthesis is [Knowledge work across projects](../../reports/Knowledge%20work%20across%20projects.md), dossiers under [research_notes/Knowledge work across projects/](../../research_notes/Knowledge%20work%20across%20projects/). What they add to the model above:

- **Human judgement is the bottleneck everywhere, and nothing rations it.** 474 relay cards waiting vs 45 done; 90 items in wondernauts' `needs_qa_human/` for one person and one Deck; 65 in sweet-street's `needs_qa/`; 27 tracelaw cards "no person has opened"; 4 in 5 theory_checks findings never adjudicated; 18 of 43 referee reports written without the skill. The human-QA designation, sampling and earned authority (#BX7B, #SJTR) are the rationing device this needs.
- **The trackers already name the proof owed.** wondernauts, tracelaw and sweet-street each built their own lifecycle folders (`needs_qa_human`, `needs_qa_llm`, `needs_review`, `needs_labels`, `needs_ab`) and none of the four Relay boards installed this week is used. Verdicts must be typed (script / metric with denominator / ground-truth recall / probe / reconciliation / level / pairwise / look-listen-play / world), with hard gate vs advisory distinguished.
- **The case is confirmed as the missing object.** Referee-Work has kept a case ledger by folder name for ten years (deadline, venue, done) with no outcome record; tracelaw keeps owner rulings in `NEXT.md`; one-off admin cases each built a small server nobody registered.
- **Prose is the universal memory and it rots**: 1,381- and 1,681-line `WARP.md`s, a five-week-stale tracker, `Last verified` headers older than the sections below them, sync-conflict copies beside originals, mappings kept in three places. State should be generated; prose keeps the why.
- **Science outputs lack provenance** (soundmatch results with no run id; 549 AIComm PDFs with no commit; outcome_test figures overwritten per run) except where someone got burned (modalities, theory_checks). Runs, hosts and artifact locations are objects the tool must hold.
- **Versioning by copying** where there is no git (three `runner_core.py`, six `load_env`, `_ff.do` pairs, `archive_05_26/`): servers need hashed versions with a changelog, skills included.
- **Decisions are lost**: dropped strategies, abandoned rebuilds, art picks and spec choices live in prose or in an `archive_*` directory with the reason gone. A decision ledger and closed-not-done cards for exploration branches.
- **Secrets, privacy and money are held by convention**: `.env` in synced trees, a key printed into a transcript that still sends, manuscripts that must not leave a case, purchases with a ceiling. Gates, not conventions.

The report's §5 states ten principles and §6 maps fourteen dimensions to objects, evidence and what Relay has today. Its recommendation: pilot on a non-software project (Referee-Work for cases, skills, privacy and levels; outcome_test or soundmatch for runs, provenance and decisions) so the claim that Relay is not only a software tool is demonstrated.

### 7. The task profile: factors that characterise a knowledge task (2026-09-23)

The owner sketched an economic reading of the choice between a person, a prompt, a skill and a program for a stream of cases: fixed cost, per-case cost, error probability, the cost of an undetected error, and how informative the verification signal is, with frequency amortising the fixed cost. Nothing formal is wanted on the card; what is wanted is the list of factors it implies, so they can be encoded in skills and Relay programs. The first six below come from that reading; the rest from the dossiers in §6.

| Factor | The question | How you'd know | What it decides |
|---|---|---|---|
| **Frequency** | How often does a case of this shape recur? | Count in the case ledger, including cases a person served by hand | Whether to build anything, and how far to harden it |
| **Regularity** | How much of the case stream is routine vs. novel? | Fallback rate: how often the server says "not mine" | Program vs skill vs person; where the intelligent joint goes |
| **Machine verifiability** | Can a script, test, metric, probe or ground-truth set tell right from wrong? | Is there an oracle that runs without a person, and is it calibrated (is its false-positive rate known)? | Whether a generate–test–repair loop is possible; whether AI output can pass without a person |
| **Human verifiability** | Can a person tell quickly, and in what form? | Pointwise vs pairwise vs criteria-first; look / listen / play / read | What the human review step looks like and what it costs |
| **Stakes** | What does an undetected error cost, and can it be undone? | Class: nuisance / rework / money / reputation / harm; reversible or not | Sampling rate, hard gate vs advisory, sign-off by rule, receipts and before-images |
| **Blast radius** | Does one defect hit one case or all of them? | Is the error in the case or in the capability? | Versioning, canaries, dry runs, rollback tooling |
| **Executability** | Can the agent run the artifact, or is the world the oracle? | Code, TeX, a model run: yes. An experiment, a rehearsal, a playtest, a client: no | Whether verification is immediate or *deferred*: "unverified until rehearsed" as an honest state |
| **Artifact form** | Text, number, code, visual, audio, running system, physical? | What the deliverable is | Which verification modes exist at all; which preview pane |
| **Rot rate** | How fast does the capability go stale? | Depends on an external UI, a model version, a data source, a person's memory | Re-verification cadence, stale flags, maintenance budget |
| **Escalation cost** | When the machine can't decide, who does, and are they the bottleneck? | Queue length on the person | Risk-ranked review queue instead of FIFO; what stays unverified |
| **Cost and money** | Tokens, time; does any step spend money or do something irreversible? | Per-case cost; purchase, publish, send, delete | Effort level; click-to-confirm gates |
| **Confidentiality** | May the case leave its folder? | Manuscript, patient, client, credential | Which panes may see it; no board quotes; secrets by reference |
| **Location** | Where do the artifact and the compute live? | Local, SSH host, cluster, Modal, Dropbox | Run ledger, host object, which checkout is canonical |
| **Batch or single** | One artifact, or a set judged against each other? | Editor, PC, hiring vs. referee, author | Pairwise/ranking vs pointwise |

Two profiles, to show the table is expressive without being formal:

- **Referee report.** Frequency ~70/yr; regularity high; machine verifiability low (citations checkable, argument not); human verifiability high but slow, pairwise only at R&R; stakes reputation, irreversible once sent; blast radius one case; executability no; artifact prose plus a level; rot low for the support-doc skill, high for the Editorial Manager scripts; escalation: the person is the server; money none; confidentiality strict; single (batch for the editor). → Skill for the support document, person for the report, case record with deadline, venue and outcome, confidential pane, pairwise view when prior reports arrive.
- **Land a commit.** Frequency hundreds/day; regularity very high; machine verifiability very high (build gate, diff check); human verifiability low and unnecessary; stakes rework, reversible; blast radius the whole checkout; executability yes; artifact code; rot low; escalation: refuse and stop; no money, no confidentiality. → Program with refusals and no human step; blast radius is why it has a dry run and a digest.

Domain purchase sits between: low frequency, high machine verifiability (API readback), money and irreversibility → program with a ceiling and a person's click.

Where the profile lives:

- **On the server** (a skill's frontmatter, a program's registry entry): the stable factors: regularity, verifiability, stakes class, executability, artifact form, rot, confidentiality, money steps. This is what the agent reads to propose verification mode and effort per task.
- **On the case**: the observed ones: cost, signal seen, escalated or not, who served it, verdict, location. Frequency, regularity and calibration are computed from cases, never declared. This answers open question 2.
- **As policy** (project or global): the thresholds: at what track record a server skips review, which stakes class always needs a person, what spend needs a click.

At intake the agent *proposes* the profile for a case it has not seen ("this looks like a referee report; here is the profile, the skill, effort high, human step required") and the owner corrects it. Corrections are ledger data.

Three cautions from the economics that the design should keep: the human signal is capacity-constrained, so the review queue must be ranked by stakes × doubt rather than served in order; a verifier whose false-positive rate is unknown (no negative controls, as in theory_checks) gives no usable signal, so measuring the verifier is itself an investment; and a skill has systematic error just as a program does (a bad prompt or a silent model update hits every case), so prompt, model and effort are one versioned unit.

### Open questions

1. When a case fails, is a card created automatically, or is the case marked failed with a *suggested* card? Owner, 2026-09-23: no single approach; likely depends on the server and the policy. A broken skill should not flood the Board.
2. What is the case record's minimum: server, version, input reference, outcome, verdict, cost, who served it (person / program / skill)? What is private (a patient, a client) and how is it kept out of the shared board? §7 proposes the answer: the observed factors on the case, the stable ones on the server.
3. Does the human-QA designation live on the server (inherited, sampled) or on the case, or both?
4. Where do the three skill sources get reconciled, and which one is authoritative for Relay?
5. Which surfaces from §4 are worth naming in the data model now, and which stay implicit until a case demands them?
6. Who may set and change a server's profile (stakes class, confidentiality, sign-off by rule): the agent that proposes it, the project owner, or a global policy?


## Decisions
- 2026-09-23, owner: the vocabulary is card / server (program, skill) / case. "A card is built, a case is served. A program serves the case algorithmically; a skill serves it intelligently."
- 2026-09-23, owner: a case served by a person counts and belongs in the same record as program- and skill-served cases ("think about a doctor or lawyer using relay to help with their case work").
- 2026-09-23, owner: there is no single rule for whether a failed case files a card automatically.
- 2026-09-23, owner: "yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user." The QA ladder is documented on #BX7B; the build is phased there as #WFRA → #1AA6 → #MSJ0 → #C3Q2 → #95VZ → the QA pane (#BX7B) → a Skills registry. This card stays the design record.
- 2026-09-24, owner: "the board should become 3 tabs, cards, skills, memories". Project skills and project memories belong in the Board alongside work cards.
- 2026-09-24, owner: "globals also needs global skills". Globals should show global skills; project memories should stay on their project's Board ("globals is showing local memories. local memories go in teh board").

## Planning notes
**What was left on the table after the QA build, and where it went (2026-09-25).** The build order recorded in Decisions ran to the QA pane (#BX7B, now the Review pane, with #JEWY's shortcut); the remainder of this card is now five planned cards, each with a plan, `## Done means`, a `verify` block and its questions in the thread:

| Left over | Card | Open questions it answers |
|---|---|---|
| §5 the Skills registry; the owner's 2026-09-24 decisions (Board = Cards \| Skills \| Memories; global skills in Globals) | #9FX8 | Q4 (authoritative source: `.relay`, settled by #HS7V), Q5 in part |
| §5 loops 1 and 2: case → card, server → card; failed-case policy per server | #G9ZD | Q1 (no single approach → `on_fail` per server with a project override), Q3 in part |
| §2 harden / soften: steps on a skill, step outcomes on a case, the hint | #4YKJ | — (follows #9FX8 and #G9ZD; needs ledger volume) |
| §4 the other objects: counterparty, runs and artifacts, schedules; the non-software pilot from report §7 | #FVVY | Q2 (the row's minimum grows by counterparty; confidential still drops references), Q5 |
| §1 / §5 point 3: earned authority end to end — sampling, promotion, demotion, AI-verdict calibration | #GW74 | Q3 (the designation lives on the server, sampled per case by policy), Q6 in part |

Still open on this card: Q6 (who may set a server's profile: the agent proposes, the owner corrects, policy floors it — the practice so far, not yet a rule) and the pilot decision under #FVVY. Housekeeping: the seven QA cards (#WFRA, #1AA6, #MSJ0, #C3Q2, #95VZ, #BX7B, #JEWY) sit in needs-verification awaiting a separate session's pass.

## Plan
**Goal.** Deliver the cards → servers → cases model in small, checkable slices, with skill maintenance from #SZ1H using the same server identity and case history.

**Findings.** The QA ladder and case foundation (#WFRA, #1AA6, #MSJ0, #C3Q2, #95VZ, #BX7B, #JEWY) and Skills registry (#9FX8) have landed but remain in needs-verification. Planned or discussing follow-ons are #G9ZD (case/server → card), #4YKJ (harden/soften), #GW74 (earned authority) and #FVVY (other objects and non-software pilot). #SZ1H owns import, catalog requirements, bundle quality and mail design.

**Sequence.**
1. Independently verify the landed QA, ledger and registry cards. Confirm a case records server identity and version, verdict, cost and confidentiality correctly before using those fields for automation.
2. Use #G9ZD to turn repeated person-served cases and server failures into suggested build/repair cards, with per-server and project policy limiting noise. Use #4YKJ to derive step-level harden/soften hints from the same case rows after enough cases exist.
3. Use #GW74 to calibrate AI verdicts against person verdicts, apply sampling and demote authority on failures. Keep the policy floor from #C3Q2 and human sign-off requirements in force; the server's profile supplies defaults, not permission to skip them.
4. Pilot #FVVY on a non-software project and record one complete path: case intake → matched server or person → artifact and evidence → verdict → server improvement card. Choose the pilot and settle profile authority under this card's existing open questions before that phase; do not treat those choices as already made.
5. Integrate #SZ1H: imported skills receive provenance and stable identity in the registry, and their Try-it and live cases use this ledger. Cross-check that a duplicate or new skill version does not combine case histories incorrectly.

**Risks.** A small or biased case sample cannot justify automatic authority; keep denominators and calibration visible. Confidential cases store references and permitted metadata, not protected content. Avoid building new panes for every object until the pilot shows a durable need.

**Verify.** Each owning card carries its own tests and verdict. The pilot demonstrates the full loop on real non-software work, and the cross-card check shows that skill versions, cases, policy and repair cards stay linked without duplicate records.
