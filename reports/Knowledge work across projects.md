# Knowledge work across projects: what twelve of the owner's own projects say about the shape Relay must take

Date: 2026-09-23. For card #1QKM (cards build servers, servers serve cases), and through it #P2W8, #BX7B, #SJTR, #R246.

Method: twelve project trees were read, not run, by eight read-only agent passes on 2026-09-23, one dossier per project under `../research_notes/Knowledge work across projects/`. Each dossier cites paths for its claims and marks what is inferred. This report is the synthesis; the numbers below come from the dossiers and were not re-derived here. Confidential material (manuscripts, referee reports, credentials) was described by structure only and not opened.

The question was set by the owner: analyse these projects, "what we were trying to do, the strengths, the weaknesses, and the limitations of a low-AI, disorganized, ad hoc, unverified workflow", bearing in mind that "dynamic work like scientific analysis, game development, and art, and one-offs like referee reports and server admin, are nothing like the static rigid step-by-step frames that envelope software development."

---

## 1. The projects at a glance

| Project | Kind of work | Size / history | Tracker | AI in the loop | Human queue |
|---|---|---|---|---|---|
| relay-terminal | software (this product) | 2,595 commits in 7 days, 1,667 AI co-authored | Relay board, 615 cards | several Claude sessions + Codex/Kimi reviewers | 474 cards waiting for a verifier, 45 done, 1 human verdict |
| tracelaw | software + legal data, in production | 62 commits (history reset; ≥6 months old), 60 AI co-authored | own `issues/` (126) + unused Relay board (1 card) | Codex + Claude sessions, 1 skill, runtime LLMs | 27 `needs_qa` "no person has opened one"; 8 owner rulings in `NEXT.md` |
| soundmatch | ML / audio paper | 1 import commit; dates inside files Jun–Jul 2026 | none; prose handoffs | agent-to-owner handoffs, GPT-5.5 as paper auditor | paper submitted; heads and embeddings unreproducible here |
| modalities | social science, LLM-coded corpus, 9 papers | 825 commits, 616 AI co-authored; coauthor 812, owner 12 | roadmap + 33 append-only task logs, 682 evidence files | agents per task, pairwise LLM judges, 3 machines | human adjudication fixtures; coauthor gates churned |
| AIComm | social science, survey experiment | 225 commits over 2 years, RA-driven, 0 AI co-authored | GitHub issues threaded through commits | offline GPT scoring, AI-written population stats | figures revised by looking, in issue threads |
| outcome_test | social science on a cluster | 19 commits, owner 0 | GitHub issues, barely | Claude Code driving Euler over SSH; PR review installed, bypassed | spec iteration by hand; strategies A–C archived |
| theory_checks | formal theory, AI review pipeline + its validation | no git, 4.1 GB, 964 paper folders | `HANDOFF.md`, 416 bytes | Codex/Sol batch runs, Fable pilot, RA kit | 4 in 5 findings never adjudicated; single coder |
| Referee-Work | refereeing, editing, PC, grants, AC | no git; 43 done cases in 2026, ten years zipped | folder per case, JLE tracker (5 weeks stale) | 9 skills, 27 EM scripts, OpenReview scripts | 18 of 43 reports written bare; no outcome record |
| ai_econ_summer_school | teaching | 54 commits, 3 instructors | README ledger, 114 entries in 4 days | agent turns ("visually checked" ×65, no images) | rehearsal never closed before day one |
| web-sites | admin with money | git with 0 commits; 1.9 GB in Dropbox | prose workflows + `transfer_state.json`; unused Relay board | scripts with agent-facing docstrings | manual registrar steps; secrets in synced tree |
| hetzner | server admin | no git; 14 runbooks, 2,808 lines | `Last verified` headers | agent-facing runbooks | health checks are paste blocks |
| wondernauts | game (Godot), art, audio | 1,965 commits, 1,353 AI co-authored; 36 GB + 241 GB evidence | own `issues/` with lifecycle folders | 27 Codex skills + conventions, GLM batch, Modal sweeps | `needs_qa_human/` 90 items, one person, one Deck |
| sweet-street | game (Godot) + server, live site | 621 commits, 551 AI co-authored; 67 GB | own `issues/` (216) + unused Relay board | overnight goals, worker manifests, three delegation regimes in a week | `needs_qa/` 65 items: look, listen, play |

Two facts frame everything else. First, **almost all of this was AI-assisted already** — eight of twelve trees are majority AI co-authored or agent-driven — so "low-AI" is not the problem; the problem is what the AI work leaves behind. Second, **four projects had a Relay board installed in the last week and none uses it**, because each already has a tracker whose lifecycle vocabulary names something Relay's does not: what proof is owed and by whom (`needs_qa_human`, `needs_qa_llm`, `needs_review`, `needs_labels`, `needs_ab`).

---

## 2. What the ad-hoc workflow does well

It would be dishonest to present these trees as chaos. The same strengths recur, and they are the seeds of the design.

- **Incidents become rules, and rules become refusals.** `land.py` and `relay-build` (relay-terminal), `release.sh` (sweet-street, "the one road to the live site", built after two faults sat on the live site for days while every gate was green), `dedupe_submissions.py` (JLE, after a duplicate directory hijacked a decision), `check-shared-nginx-listeners` (hetzner, after every tunnelled hostname served the wrong page), the biome contract test (wondernauts, after 170 level files were silently ignored). Every one is a card that built a server, and the server refuses the action that caused the incident.
- **Provenance where someone got burned.** modalities has run identities, prompt fingerprints, release hashes and bundle locks, each refusing silent drift; theory_checks freezes prompt, sources and runner into `audit.zip` with SHA-256s; sweet-street stamps `build_info.json` into the shipped site; web-sites keeps per-domain, timestamped `transfer_state.json`.
- **Honesty about what is unverified.** The best documents say what they did not check: hetzner's "Known gaps", modalities' "Do not count a fixture test as completion of a required real-data or remote test", wondernauts' "Exit status is not evidence", sweet-street's "required human/device review stays open", theory_checks' `Pending` until a record exists.
- **Criteria written before the human looks.** wondernauts' `manual_playtest_queue.md` carries "Human Pass Criteria"; sweet-street's issues carry an `Acceptance` field; sweet-street's `needs_qa` notes must say "what a machine could not establish".
- **Judgement from the artifact, not the description.** Art was chosen "from images, not descriptions" (sweet-street `STYLE.md`); rejected work archived, not deleted. AIComm's exhibits were revised by posting the PDF in an issue thread.
- **Money and irreversibility handled by hand on purpose.** soundmatch: "this spends money, which is why no script here calls publish"; web-sites' purchase script requires `--max-price` and hands over to the person above a ceiling; outcome_test's API pilot logs every request with its cost.
- **One folder per case, a naming rule, and an archive** (Referee-Work: ten years, checksum catalogue). The simplest possible case ledger, and it has worked for a decade.

---

## 3. What it does badly, and what it costs

### 3.1 The person is the bottleneck, everywhere

relay-terminal: 474 cards waiting versus 45 done, one recorded human verdict. wondernauts: 90 items in `needs_qa_human/` for one person with one Steam Deck. sweet-street: 65 in `needs_qa/`, each needing looking, listening or playing. tracelaw: 27 in `needs_qa`, "no person has opened one"; 8 rulings queued in `NEXT.md` for 17 days; 3 of 50 adjudications done. theory_checks: four in five findings never adjudicated, every comparison single-coder. Referee-Work: 18 of 43 reports written without the support document. Summer school: rehearsal, the only pedagogical check, still open on the first day.

The machines now produce more than one person can judge, and every project has quietly accepted an unbounded queue of "awaiting human". This is the central fact for #BX7B: human judgement is the scarce resource, and none of these systems rations it. Nothing samples, nothing prioritises by risk, nothing lets a server earn the right to skip review.

### 3.2 Prose is the universal memory, and it rots

`WARP.md` is 1,381 lines in wondernauts and 1,681 in tracelaw. The summer school README carries a 114-entry ledger typed by an agent. theory_checks' handoff is one 416-byte line. Trackers and headers go stale by construction: the JLE tracker is five weeks behind its queue folders; hetzner's `Last verified: 2026-08-12` sits above a section dated 2026-09-18; the summer school's deck mapping is kept in three places that already disagree; soundmatch's `README.md` describes a pipeline that is not the one used. Sync-conflict copies of runbooks and skills sit beside the originals in hetzner, Referee-Work and the summer school, so every grep reads both.

Prose is good at *why* and terrible at *what is*. Every "keep current" section, inventory, tracker and status header in these trees is state that a program could regenerate and a person is instead retyping.

### 3.3 Outputs without provenance

Science is the worst case. soundmatch's `results/*.json` carry no run id, commit or config hash; the heads and embeddings live on a Modal volume and a local path that does not exist. AIComm has 549 PDFs in git and no link from any PDF to the commit that made it; its GPT text scores are an opaque `.dta` whose prompt, model and date survive only in a script with a redacted key. outcome_test's 68 PNGs are overwritten on every run and the config that ran lives on the cluster, not in the repo. wondernauts' art picks point at files under `~/.codex/generated_images/`. Where provenance exists (modalities, theory_checks) it was retrofitted after an inspected build showed empty version strings.

### 3.4 Versioning by copying

Where there is no git, or git is not trusted for it, versions are copies: three `runner_core.py` with three checksums (theory_checks), three generations of the same KB articles plus six `sed` patches (tracelaw), `data_cleaning.do` beside `data_cleaning_ff.do` (AIComm), `archive_05_26/` beside the modules "derived from" it (outcome_test), `load_env` reimplemented six times (web-sites), a `deploy.sh` per site. Four trees have no version control at all (theory_checks, Referee-Work, hetzner; web-sites has git with zero commits and no `.gitignore`, so `git add .` would sweep in `.env`, `credentials.txt` and 748 MB of backups).

### 3.5 Servers built once and forgotten

Good one-off servers exist everywhere: the Mattermost onboarding scripts with a hashed state file (hetzner), `verify_migration.py` with 20 route checks (web-sites), the X API pilot with a findings memo and cost manifest (outcome_test), the EJ guest-issue scoring scripts, the Prolific study harness (soundmatch). None is registered anywhere, so the next similar case starts from scratch, and the deploy pattern was re-implemented per site instead of once.

### 3.6 Skills exist, but nobody routes to them

Referee-Work has nine skills covering every editor decision except accept, and 13 of 43 cases used the support-document skill while 18 got a bare `report.txt` — with no written rule for which. wondernauts has 27 Codex skills sharing a `CONVENTIONS.md`; tracelaw has one; the summer school's dominant loop (edit, rebuild, look, log — repeated 114 times) has none; soundmatch's paper-versus-results audit was done three times by a model and is not a skill. A conflicted-copy `SKILL.md` sits beside the live one. Skills also depend on external UIs (Editorial Manager, Porkbun, Amazon) that change without notice, and nothing detects when one has rotted.

### 3.7 Location is a hidden variable

Three sync mechanisms across two machines (wondernauts: Mutagen, Syncthing, git); the canonical checkout is "one machine's at a time" (sweet-street); byte-pinned audio suites broke when work moved hosts; modalities serialises three machines with `/tmp` locks; outcome_test's running config is the cluster copy; tracelaw's production is a one-way sync of the laptop tree, so uncommitted edits run live and a cloud reviewer once declared the monitor dead because its files were untracked. Which machine holds which artifact, which checkout is live, and which host may see which data are facts no tool holds.

### 3.8 Secrets and privacy by convention

`.env` files sit in Dropbox-synced trees in web-sites (mode 0644), Referee-Work (inside the case folders), hetzner (beside an `age` identity, with the trade-off argued in prose). An SES key "printed into a transcript on 2026-08-16" still sends mail (tracelaw). theory_checks needs a wall between what the model may see and the publisher versions, enforced by README. Referee-Work needs manuscripts and reports never to leave the case. All of this is held by care.

### 3.9 The cost, where it is on record

872 of 6,384 published legal findings removed by a model with no human sample, and 2,538 published findings that for months "rested on the model's memory of the cited case", leaving "false accusation[s] about a named lawyer's work" (tracelaw). Two faults live on a game site for days while every gate was green (sweet-street). A green build of code nobody had written; five commits that silently reverted other sessions (relay-terminal). A backfill silently held for 15 days by a disk guard with 879 jobs waiting (tracelaw). 170 level files ignored by a loader that chose by spelling (wondernauts). 23 of 75 batch jobs failed on quota with the remedy "watch for the message by eye" (theory_checks). These are the incidents that produced the rules in §2; the rest of the unverified work simply has not failed visibly yet.

---

## 4. Dynamic work is not software work, and the trees show how

The owner's worry is right, and the dossiers make it concrete.

**Software** has a spec, an artifact that is code, an oracle that is a test, and a lifecycle that ends. Even here, three of the four gates that matter (relay-terminal's land gate, sweet-street's release road, tracelaw's re-projection receipts) were about *process incidents*, not code correctness, and the verification backlog is human, not mechanical.

**Science** has a goal that moves. outcome_test tried strategies A through E and archived three with no recorded reason; wondernauts attempted a v2 rebuild and demoted it to "a preserved donor"; soundmatch had three names. The artifact is a claim plus the evidence for it, and the verifying question is not "does it pass" but "does the evidence support the claim at this level of confidence" — which is a level, sometimes a metric with a confidence interval, never a boolean. What science needs recorded, and no project except the summer school and outcome_test's YAML comments records, is the **decision**: which branch was dropped, when, and why. An `archive_05_26/` directory is a branch with its reason lost. Exploration is a search; the card model fits it only if a card can be a branch that is closed without being done.

**Games and art** have an oracle that is a person's perception and preference. Both game repos say this in their own rules: "AI or vision review is advisory … It never overrides deterministic hard gates" (wondernauts), "a person looks at a screenshot" (sweet-street). The machine can gate hard failures (parse, parity, traversability ceilings) and can never say *good*. The right shape is already there in those trees — machine gates, then a human verdict against criteria written first, pairwise for art — but it is implemented as folders and prose, and the human queue is unbounded.

**One-offs** (a referee report, a server migration, a cohort onboarding) have no build phase at all: the *case* is the unit. Their value is in the case record (deadline, role, venue, outcome — none of which Referee-Work keeps beyond the folder name) and in the fragments they leave behind that the next case could reuse (a support document, a verifier script, a runbook with probes). Each of these one-offs did build a small server; none registered it.

**Teaching and admin** sit between: a repeated loop (edit-build-look; health-check-fix-verify) served by a person driving an agent, with a verification that only the person can do (rehearse; know that the server is up because you asked). The honest record is "unverified until rehearsed", and the summer school instead has model-estimated timings standing in for a rehearsal.

So the shared frame across all of these is not step-by-step. It is **claim → evidence → judge**, with the judge varying: a test, a metric with a denominator, a ground-truth set, a probe on the live artifact, a reconciling total, a level, a pairwise preference, a person looking, listening or playing, or the world. Cards for building fit software. For everything else the case is primary and the card is an occasional act of building a server that will serve the next case better.

---

## 5. Principles

Ten, each with the trees that taught it.

1. **Human judgement is the scarce resource; ration it.** Every project has an unbounded "awaiting human" queue. Designate per server whether a person must judge, sample when a server has earned it, prioritise by risk, and write the criteria before the person looks. (relay-terminal, wondernauts, sweet-street, tracelaw, theory_checks.)

2. **Every artifact names its oracle.** Script, metric with denominator, ground-truth set, probe, reconciliation, level, pairwise, look/listen/play, or world. An artifact with no named oracle is unverified and must say so, in a field, not a footnote. (sweet-street's `Acceptance`; wondernauts' lifecycle folders; theory_checks' `Pending`.)

3. **Hard gates and advisory judgement are different things and must not be confused.** A machine check that can fail the work is a gate; an AI opinion is advice until it has been measured against a ground truth. Both game repos state this rule; #SJTR's earned-authority rule is the same one. (wondernauts `CONVENTIONS.md`, theory_checks' proxy precision with null false-positive cells.)

4. **Record state from the system; type only the why.** Trackers, inventories, `Last verified` headers, deck mappings and status documents should be generated from the queue, the host, the build and the cards. Prose is for reasons and incidents. (JLE tracker, hetzner, summer school, soundmatch README.)

5. **Provenance is cheap at write time and impossible later.** Every output carries a run id, code hash, config, model and prompt version, host, and cost; every judgement carries the revision judged and who judged it. (modalities and theory_checks as the positive cases; soundmatch, AIComm, outcome_test, wondernauts art as the negative ones.)

6. **Servers are versions, not copies.** A skill or program is a hashed unit — prompt, runner, model, effort, conventions — with a changelog and an eval record, and "v3" is a diff from "v2" with a reason. Skills rot when the world changes; the case ledger is how you notice. (theory_checks' three runners, tracelaw's article generations, Referee-Work's conflicted `SKILL.md`.)

7. **Incident → rule → refusal, and keep the link.** Where a rule can become a tool that refuses the bad action, make it one, and keep the card that built it attached to the server so the refusal message and the reason do not drift apart. (`land.py`, `release.sh`, `dedupe_submissions.py`, the listener guard.)

8. **Decisions are objects.** A dropped strategy, an art pick, a winsorisation choice, an owner ruling, a scope cut — dated, with a reason, attached to what it affected, and revisitable. (`NEXT.md`, `STYLE.md`, `review_status.json`, the summer school ledger, `archive_05_26/`.)

9. **Location is a fact the tool holds.** Which host runs it, which checkout is canonical, which volume has the data, which machine may see which evidence, which sync is authoritative. (wondernauts' three syncs, modalities' three machines, outcome_test's cluster copy, tracelaw's one-way production sync, soundmatch's lost Modal volume.)

10. **Privacy, money and irreversibility are gates, not conventions.** Confidential-by-default for case material; secrets by reference, never printed, stored outside synced trees; spending and publishing require a person's click and leave a receipt. (Referee-Work, theory_checks' stage wall, web-sites' purchase ceiling, soundmatch's Prolific rule, the SES key in a transcript.)

---

## 6. Dimensions Relay could support

Each row is an object or surface the trees are already approximating by hand, with the projects that supply the evidence and the nearest thing Relay has today. This extends the object table on #1QKM.

| Dimension | What the object holds | Evidence | Relay today |
|---|---|---|---|
| **Case** | role, venue, counterparty, deadline, folder, server that served it (person / program / skill), outcome, cost, verdict | Referee-Work folders; tracelaw `NEXT.md` rulings; hetzner onboarding state; wondernauts playtest intake | nothing; every case becomes a card or a folder |
| **Server registry** | programs and skills with version hash, provenance, declared oracle and effort default, human-QA rule, case history, eval results, stale flag, routing by case type | Referee-Work's nine skills and no routing rule; wondernauts' 27 skills with conventions; theory_checks' frozen copies; six `load_env`s | skills as loose files from three sources; Tests for programs only |
| **Run / job ledger** | remote compute (GPU, SLURM, Modal, batch plans), args, code hash, config, host, quota failures, reruns, outputs written | soundmatch Modal runs; outcome_test `sbatch`; theory_checks plans and `watch_status.json`; tracelaw's 15-day silent worker | Activity records turns; nothing records runs |
| **Artifact with provenance and location** | which run made it, which revision, where it lives (repo, host, volume, Dropbox), superseded-by | soundmatch results; AIComm PDFs; outcome_test PNGs; wondernauts art; modalities releases | git for code; `docs/qa_evidence/` for screenshots |
| **Typed verdicts** | script / metric with denominator / ground-truth recall / probe / reconciliation / level / pairwise / look-listen-play / world; hard gate vs advisory; who judged, which revision | every project; the lifecycle folder names in wondernauts, tracelaw, sweet-street | `## QA checklist` and `## Verdict` sections; `needs-qa` vs `needs-verification` |
| **Human-QA pane** | the queue of person-only judgements with device, entry point, pass criteria, sampling rate, and a place for notes; "unverified until rehearsed/played" as a state | `needs_qa_human/` 90; `needs_qa/` 65; rehearsal TODOs; adjudication in `notes` strings | #BX7B (discussing) |
| **Decision ledger** | dated decision, reason, what it affected, superseded-by; dropped branches as closed-not-done cards | summer school README; `STYLE.md`; `NEXT.md`; `archive_05_26/`; the v2 rebuild | `## Decisions` on cards, free text |
| **Host / location** | servers, clusters, volumes, checkouts: IP, services, ports, `last verified` refreshed by a probe, what data it may see, which sync is authoritative | hetzner runbooks; wondernauts three syncs; modalities three machines; outcome_test Euler | SSH tools with host explicit; no host object |
| **Secrets by reference** | named keys, values never printed, store outside synced trees, file-mode check | web-sites `.env` 0644; Referee-Work `.env` in cases; the SES key in a transcript | Options › Security (#3KB7) |
| **Doctor for drift** | stale `Last verified`, sync-conflict copies, duplicated scripts, tracker vs queue disagreement, README vs runbook, 0-commit git with secrets, untracked production files | hetzner, Referee-Work, summer school, soundmatch, web-sites, tracelaw | `land.py doctor` for the index only |
| **Multi-agent ownership** | who claims which paths on which machine, contested hunks, serialised engine sessions | `land.py`; modalities `/tmp` locks; sweet-street's "disjoint file ownership" with no tool | `land.py` in this repo only |
| **Money / irreversibility gates** | spend ceilings, publish and purchase as person-click actions with receipts | web-sites `--max-price`; soundmatch Prolific; outcome_test cost manifest; modalities' 1,000-cluster refusal | nothing |
| **Privacy walls** | which pane may see which folder; confidential-by-default cases; no card may quote case material | theory_checks stage wall; Referee-Work; summer school student/solution split | nothing |
| **Generated status** | board, tracker, inventory and status documents built from cards, cases and hosts, never typed | `issues/BOARD.md` is generated; every other tracker is typed | board only |

Not fourteen buttons. But the data model should name these so a card can point at a case, a case at a server, a server at its runs, a run at its artifacts, an artifact at its verdicts, and a verdict at the person or model and the revision it judged.

---

## 7. What this means for #1QKM

- The **case** is confirmed as the missing primary object. Referee-Work has kept a case ledger by folder name for ten years; tracelaw keeps owner rulings in a text file; games keep playtests in `.docx`. The case record — with person-served cases included — is the eval data for every server and the thing that says what to build next.
- **Skills need a registry with routing and rot detection** before they need more skills. The evidence is 13 of 43 referee cases using the skill with no rule, and skills that drive external UIs with nothing that notices when the UI changes.
- **Verdicts must be typed and carry a denominator.** The single most reused idea across the trees is a lifecycle folder named for the proof owed; the second is a percentage with its numerator. Relay's `needs-qa` / `needs-verification` pair is too coarse for any project here except its own.
- **The human-QA pane (#BX7B) is a rationing device**, not a review screen. Its job is to make the queue finite: designation on the server, sampling once earned, criteria written first, and "unverified" as an honest state.
- **Runs, hosts and locations are the workspace side (#P2W8)**: an editable artifact with a preview is half the story; the other half is where the build ran and where the output lives.
- **Start with a non-software pilot.** Every principle above was learned from a software incident and is already half-built there. The projects that would prove the model are Referee-Work (cases, skills, privacy, levels) and outcome_test or soundmatch (runs, provenance, decisions). If Relay serves those two well, the claim that it is not only a software tool is demonstrated rather than asserted.
