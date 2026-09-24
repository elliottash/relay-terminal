# theory_checks — dossier

## What it is and what we were trying to do

`theory_checks` asks whether AI reviewers can find substantive mathematical errors in published economic-theory papers, and how bad those errors are. Domain: formal theory / research methodology. Not a git repository (verified: no `.git`); it lives in Dropbox (`/home/elliott/projects` symlinks to `/home/elliott/Dropbox/_Projects`) and is shared with a collaborator whose machine paths appear in the code (`known_error_validation/first10/refresh_report.py` hardcodes `/Users/cicca/.../theory_checks`). Size: 4.1 GB, 571 `.py` files, 964 paper folders under `Papers/` (600 `corpus_*`, 263 DOI-named, 100 `known_error_*`, 1 `blind_*`).

Two products. First, a **server**: an AI review pipeline that takes a PDF and returns typed findings, in three generations that are each a frozen copy of the last: `neutral_discovery_pilot/`, `neutral_discovery_multi_provider/` ("frozen copy of the validated engine", `WORKFLOW_HELP.txt`) and `graph_guided_neutral_discovery/`. Beside them `claim_graph_framework/` reconstructs the paper's argument as a typed hypergraph, maps findings onto it, constructs the weakest valid repair and scores severity deterministically (`docs/architecture.md`). Second, the **verification of that server** against ground truth: `known_error_validation/` runs the blind reviewer on papers with published corrigenda and scores recall.

## How the work was actually done

- **Handoff state** is `HANDOFF.md`, 416 bytes, one timestamped line: a plan "failed instantly" because the runner copied the operator's Codex auth, and the relaunch set `PILOT_CODEX_AUTH_FILE`. Everything else about session order is inferred from folder names and JSON.
- **AI tools**: Codex/Sol through the subscription CLI is the workhorse ("no API key or metered API call was used", `claim_graph_framework/results/besley_persson_2009/RESULTS_SUMMARY.md`); Claude/Fable ran a pilot and four `next40` papers; Kimi and Codex reviewed the website design (`claim_graph_framework/docs/design_reviews/2026-08-10-{codex,kimi}.md`). One Codex session left `.codex_artifacts/<uuid>/build_publisher_inventory.mjs` and `outputs/<uuid>/*.xlsx` with PNG previews.
- **Runbooks for agents**: `ra_batch_kit/AGENTS.md` and `CLAUDE.md` are byte-identical and list what an agent on an RA's laptop must never do ("Never work around a REFUSED message"). The three `WORKFLOW_HELP.txt` files are operator manuals.
- **Scripted**: plan creation, job isolation, artifact validation, compaction to `review.tex/pdf/json + audit.zip`, graph schema recovery, scoring, report rendering. **Manual**: choosing papers, adjudicating findings against corrigenda (`comparisons/*.json` carry `status: complete_single_coder`), deciding when to rerun (`plans/known-errors-first10-sol-rerun-paper7-v1`).
- **State between runs** is the plan folder (`neutral_discovery_multi_provider/plans/<plan>/state/j*.json`, 25 plans) and `watch_status.json` per batch. Versions are folder suffixes: `clean-v1/v2/v3`, `continuation-v1/v2/v3`.

## Cases and servers

- *Review one paper blind* — a program wrapping a model: one fresh CLI process per iteration, frozen prompt hash, source hashes, no cross-iteration contamination. 152 paper folders under `MultiProviderReviews/`, 12 under `Reviews/`, 6 under `GraphGuidedNeutralReviews/`.
- *Extract the claim graph and score severity* — program plus three prompts (`graph_v1`, `link_v1`, `repair_v1`); `audit_v1.md` is frozen with its SHA-256 in `prompts/audit_v1.sha256` (verified matching). 107 result folders hold `graph.json`; two hold the full `REPORT.md` chain.
- *Run a batch on an RA's machine* — `ra_batch_kit/run_batch.sh`: idempotent, three guards (assignment file, `LOCK`, model probe), model fixed to `gpt-5.6-sol` at `xhigh`. `claim_graph_batches/assignments.csv`, which it requires, does not exist, so I infer no RA has run it against this folder yet.
- *Build the validation report* — `refresh_report.py`, whose paths belong to the collaborator's Mac, so this server runs on one laptop only. `next40/reports/` and `next50/reports/` both contain `first10_validation_report.*` (verified): the name never followed the batch.
- *Redact papers for blind review* — `paper_errors_correction/anonymize_papers.py` (72 originals, 72 anonymized).

Redone by copying: three `runner_core.py` with three checksums and two differing `direct_neutral_v1.json` (verified by md5 across the three discovery folders). Freezing by copy was the versioning mechanism.

## Verification

**Ground truth.** `paper_errors_correction/economics_substantive_theoretical_corrections.csv` catalogues 118 published corrections (columns `Error identified`, `Outcome`, `Confidence`; 34 "Result repaired", 34 "Stronger assumptions/domain", 20 "Claim false"; 110 High confidence). Separation is enforced by folder and prose: corrigenda "are used only after model outputs are frozen" (`known_error_validation/first10/README.md`); the RA kit forbids corrections in `Papers/`.

**Scoring protocol** (`comparison_template.json`, schema `known-error-comparison-v1`): each documented error gets `detected`, `match_quality` (exact/partial/miss), `type_match`, `graph_claim_relevant`, `graph_severity`; each finding gets `validity`; a `negative_controls` list exists for verified-clean claims. The single-run rule (adopted 2026-09-07) scores each run alone; the second run is a stability check. Every percentage carries numerator and denominator.

| batch | cases | recall (run 1) | proxy precision | claim-relevant precision | status |
|---|---|---|---|---|---|
| first10 | 10 | 9/13 | 9/54 | 8/31 | 10/10 compared, 2 runs |
| next40 | 40 | 54/82 | 47/209 | 43/127 | 40/40 compared; 23 of 75 jobs failed |
| next50 | 50 | none | none | none | 0/50 compared, 37 jobs pending |

(from each batch's `report_data.json`; first10 run 2 gives 9/13 and 9/46; run agreement 8 both, 1+1 single, 3 neither.)

**False positives are the honest gap.** "Precision" is proxy precision: findings that matched a documented error. The rest are `additional_unadjudicated`, not false positives, because a corrigendum "is not treated as an exhaustive audit of the original paper" (`comparisons/known_error_0001.json`). `false_positive` and `true_negative` are `null` in every confusion matrix because `negative_controls_scored` is 0 in every batch. Roughly four in five findings have never been adjudicated, and every comparison is `complete_single_coder`.

**Modes present**: script (28 dependency-free scorer tests, schema validation of every model output, hash freezing); metric against ground truth (recall); levels (`match_quality`, `Confidence`); AI-on-visual only for redaction QA (`tmp/pdfs/batch_qa/` pairs `b1_blind_p1` with `b1_source_p1` PNGs, inferred to be page comparisons). Severity stays `model_provisional` "until human adjudication", and the pilot's 80 percent is "not an estimate that the paper is 80 percent wrong" (`claim_graph_framework/README.md`).

**Unverified**: the extracted graphs (a theorist "must review the extracted graph, every finding-to-graph mapping"); the `next40/comparisons_quarantine/` bundles; comparability of the four Fable papers with the Sol runs in the same batch.

## Strengths

- Inputs and outputs are frozen and hashed; `audit.zip` carries prompt, sources, events and the runner, so any review can be re-derived.
- The evaluation refuses to invent numbers: `Pending` until a record exists, numerators everywhere, pooled figures demoted to `reference_union_pooled`.
- Fragility and substantive severity are kept as two objects, with a README warning against conflating them.
- The RA kit makes a research pipeline runnable by a non-author, with refusals instead of instructions.

## Weaknesses and limitations of the ad-hoc workflow

- **No history**: no git, so drift shows only as divergent copies of `runner_core.py` and `direct_neutral_v1.json`, with no diff or reason.
- **Handoff is one line**: why `clean-v1` was abandoned, why papers 7 and 8 were rerun, why `next50` stalled at 9 succeeded / 37 pending is nowhere.
- **Report server bound to one laptop**: `refresh_report.py` reads graphs from `/Users/cicca/Documents/jarvis/...`; the owner cannot rebuild the report here.
- **Misnamed and stale outputs**: `next40/reports/first10_validation_report.pdf`; `next50/watch_status.json` last touched 2026-09-08 while `HANDOFF.md` is 2026-09-16.
- **Quota as an unmodelled failure**: 23 of 75 `next40` audit jobs failed; the kit's remedy is to watch for "You've hit your usage limit" by eye.
- **tmp sprawl**: `tmp/pdfs/` has 8 subfolders and 282 files (`vendor/`, `claude_audit_restore/`), plus `claim_graph_framework/_tmp` and `tmp`.
- **Adjudication is single-coder and unlogged**: who compared, when, and with what doubts lives in `notes` strings.

## What Relay would have to support here

- **A server object with frozen versions**: prompt, runner, model and effort as one hashed unit, so "v3" is a diff from "v2" with a reason, not a copied folder.
- **Batch runs as a pane**: jobs, quota failures, reruns and continuations shown as one plan with state, replacing `watch_status.json` plus a one-line handoff.
- **Ground-truth evaluation as a verification mode**: a case set with documented errors, a comparison schema, and a metric card that shows numerators and marks `Pending`; the report must build on any machine.
- **Adjudication as a task type**: pointwise verdicts on findings (match, miss, additional) with coder identity, a second coder, and the negative-control list that is always empty today.
- **Privacy walls between stages**: the model may see `Papers/` and never `publisher_versions/`; enforced per pane, not by README.
- **Delegated runs for RAs**: the kit's refusals (assignment, lock, model probe) as policies on a shared run, including the assignment file that is currently absent.
