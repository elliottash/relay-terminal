# soundmatch — dossier

## What it is and what we were trying to do

An ML/audio benchmark paper. "SoundMatch-SR" (`README.md`), later "Doppelganger: Sound Effects and Their Synthetic Twins" (`ARXIV_SUBMISSION.md`): 10,420 real sound-effect clips paired with synthetic twins, a 7-class DCASE corpus, five frozen encoders and three small trained heads. The finding is a dissociation: instance matching transfers to unseen categories, category structure does not (`PAPER_DRAFT.md`, `UCS_RESULTS.md`). Single author, ETH Zürich; arXiv `2607.04337`, 19 pages (`OUTREACH_CONTACTS.md`).

State: paper submitted, outreach drafted, repo frozen. Size (verified): 1,088 tracked files, 802 of them mp3 stimuli in `human_study/audio/`; 69 `.py`, 41 `.md`, 34 `.png`, 11 `.tex`. Python plus Modal for GPU, LaTeX, a stdlib HTTP server for the human study.

**One commit.** `403c57d` "Import standalone soundmatch with source provenance and package smoke validation", 2026-09-05. `migration/README.md` names the source: a recovered worktree of a larger repo, where this lived under `docs/research/doppelganger/` (`migration/source_manifest.json`); the full history is a mirror outside this repo. So the single commit says nothing about how it was worked and everything about how it was kept: the research was done inside a game project's docs folder, never its own unit, and extracted after the fact. Dates inside the files run 2026-06-29 to 2026-07-04 (`HANDOFF_SFX.md`, `paper/REVIEW_clarity_consistency.md`).

## How the work was actually done

Inferred from the documents, which are almost all agent-to-owner handoffs:

- **Blueprint as contract**: `BENCHMARK_BLUEPRINT.md` ("This document is the contract"; research questions mapped to tables). `RESULTS_SUMMARY.md` then opens "What the blueprint promised but the delivery environment could not run (no GPU/weights), now run for real" and "The two heads you asked for": an AI session reporting to the owner.
- **AI tools**: a `.claude/settings.local.json` existed at the source and was not imported (`migration/source_manifest.json`). GPT-5.5 was the auditor: `paper/AUDIT_gpt55.md`, `paper/REVIEW_clarity_consistency.md` ("via the codex/OpenAI Responses API"), and `ARXIV_SUBMISSION.md` "Numbers audited (GPT-5.5, three rounds)".
- **Parallel sessions**: `modal_ssl.py` header: "kept SEPARATE from modal_app.py so this work never conflicts with concurrent edits there".
- **Scripted vs manual**: GPU runs are commands typed from `RUNBOOK.md`; nothing records which ran. Publishing the Prolific study is manual by design: "this spends money, which is why no script here calls publish" (`human_study/README.md`).
- **State between sessions**: prose handoffs and "Status & next steps" lists; no board. Runs are named by filename tag (`kfold_scores_beats_ucs_paired.json`, `--tag invariant_noA` in `RUNBOOK.md`).

## Cases and servers

- *Embed corpus with encoder E; train head with objective O* — program (`modal_app.py`, `modal_ssl.py`, `src/bridge.py`), driven by hand.
- *Regenerate tables and figures* — program: `src/analyze.py`, `src/make_figs.py`, `scripts/ssl_dissociation.py` write `results/`.
- *Apply a head to new audio* — a server built for another project: `src/apply_head.py`, "the integration point" (`HANDOFF_SFX.md`).
- *Run the human study* — a real server: `human_study/serve.py`, `task.html`, `build_trials.py sample|fetch|transcode|verify`, `analyze.py`; 52 response files.
- *Audit paper numbers against `results/*.json`* — a skill served by GPT-5.5; resolution recorded in `paper/REVIEW_clarity_consistency.md`.

Redone ad hoc: the pipeline exists twice. `README.md` and `scripts/run_all.sh` describe a local-GPU flow through `src.manifest`; `RUNBOOK.md` describes the one used (`src.manifest_dcase`, Modal). `README.md` is byte-identical to `docs/history/synthmatch_embeddings/README.md` and lists 8 of the 27 modules in `src/`.

## Verification

- **Script**: `tests/test_metrics.py` (hand-computed values) and `tests/test_pipeline_synthetic.py` (gap must grow with injected shift); `migration/smoke-results.json`: 5 passed. No CI.
- **Metric**: the artifact is a metric table with bootstrap CIs (`results/leaderboard.md`, `kfold_scores*.json`). Verifiable in principle, but nothing links a JSON in `results/` to the code, config and Modal run that produced it.
- **AI-on-text**: the GPT-5.5 audit found a possibly sign-flipped AUC and an over-broad claim (`paper/AUDIT_gpt55.md`).
- **AI-on-visual**: `PAPER_FEEDBACK.md` comments on overlapping labels in Figure 3b.
- **Human, pairwise and pointwise**: 2AFC and 6-way listening trials, with model numbers on the same trials (`human_study/README.md`).
- **Unverified**: the heads and embeddings themselves. `migration/external-data.json` records `/home/elliott/data/doppelganger` as `exists_locally: false` and the Modal volume "not revalidated"; `issues/reproduce-research.md` is open. The IRB status the audit marked BLOCKING is resolved nowhere in the tree.

## Strengths

- Metric-native: every claim has a number with a CI, and the metric code is tested against hand values.
- Handoffs are good prose: `HANDOFF_SFX.md` says what generalises and what does not; `RUNBOOK.md` is exact commands.
- The human study is a complete instrument with anti-cheat aliasing and a frozen design (`trials.jsonl`).
- The migration was honest: "historical claims, not newly reproduced findings" (`migration/README.md`).

## Weaknesses and limitations of the ad-hoc workflow

- **No history** in the repo; one import commit.
- **Results not traceable**: `results/*.json` carry no run id, commit or config hash; the commands live only in prose.
- **Data absent**: heads, embeddings, manifests are on a Modal volume and a local path that does not exist here.
- **Stale, duplicated docs**: `README.md` versus `RUNBOOK.md`; `scripts/run_all.sh` cannot run the real pipeline; three project names (`SoundMatch-SR`, `Doppelganger`, `SynthMatch` in `pyproject.toml`).
- **Duplicated artifacts**: the same figures in `results/`, `arxiv/`, `paper/`, `_archive/figures_teaser/`; a stale `_archive/PAPER_DRAFT.synctex(busy)` lock; `results/leaderboard.md` shows `instance MRR nan`.

## What Relay would have to support here

- A **run ledger** for remote GPU jobs: each `modal run` with args, code hash and the files it wrote, so a table can name its run.
- **Paper-versus-results audit as a repeatable skill**, verdict kept beside the `.tex`, re-run after edits.
- **Handoffs as cards**: `HANDOFF_SFX.md` and `RESULTS_SUMMARY.md` are cards in prose; they need status and a successor.
- **Money and irreversibility gates**: Prolific publish and paid generation modelled as "requires human click".
- **Artifact location tracking** (Modal volume, `~/data`, lost); `migration/external-data.json` is the shape.
- **Doc staleness** flagged when a README diverges from the runbook or the `src/` listing.
