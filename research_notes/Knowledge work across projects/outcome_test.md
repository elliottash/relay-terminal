# outcome_test — dossier

## What it is and what we were trying to do

"Outcome Test of Algorithmic Bias" (`README.md`): does Twitter's recommendation algorithm change downstream engagement? Firehose snapshots of retweets around six event dates (`code/config/events.yaml`) are used for quasi-experimental designs on the algorithm's own thresholds: a score cutoff of 0.21 and a follower/following-ratio kink at 0.5 for accounts following over 500 (`code/config/analysis_config.yaml`). Strategies A–E were tried; the live pipeline runs D and E (`code/analysis/run_analysis.py`). Coauthors: Rafael Jimenez Duran created the repo; a PhD student ("Alex-Pin", `weipin@ethz.ch` in the SLURM scripts) wrote most of it; Elliott has no commits.

State: analysis in progress, last commit 2026-08-17. Size (verified): 19 commits, 2025-02-08 to 2026-08-17, 11 by Alex-Pin, 8 by Rafael; 2 carry a Claude `Co-Authored-By`. 174 tracked files: 68 `.png`, 38 `.py`, 17 `.csv`, 15 `.pyc`, 8 `.sh`, 5 `.R`, 4 `.yaml`, 4 `.ipynb`, 3 `.md`. Python (pandas, scipy) on the Euler cluster via SLURM, R for the X API pilot. `README.md` is three lines: GitHub for code and figures, Dropbox for raw data, Euler for "code, all the data files".

## How the work was actually done

- **Three homes**: the repo is a partial mirror of the cluster. Commit `ab87993` "move codes from cluster to repo"; `analysis_config.yaml` hardcodes cluster paths for data, output and checkpoints; `/data` is a gitignored symlink to Dropbox (`.gitignore`).
- **Task tracking**: GitHub issues, lightly ("new specs #8" is the only reference). The only architecture document, `code/analysis/archive_05_26/README.md`, describes the previous pipeline.
- **AI tools**: `.github/workflows/claude.yml` (`@claude` on issues and PRs) and `claude-code-review.yml` (review every PR) were added on 2026-04-30 by PR #6. Since then every commit went straight to `main`, so the review workflow has had nothing to review. `.claude/settings.local.json` allows `Bash(ssh Euler:*)`: Claude Code was driving the cluster over SSH.
- **Scripted vs manual**: sampling and cleaning are SLURM array jobs over `events.yaml` (`code/process/submit_batch.sh`); analysis is one `sbatch` (`code/analysis/slurm/submit_analysis.sh`, 256 GB, 24 h). Iteration on specifications was manual: "a bunch of fixes on spec", "delete some old figs".
- **Run naming**: outputs are named by strategy, outcome and bandwidth (`output/figure/2020-12-16/strategy_d/rkd_D_time_to_rt_3_bw0p02.png`), not by run. Each rerun overwrites.
- **The X API pilot** (2026-08-17) is the one well-recorded episode: `output/data/x_pilot/FINDINGS.md` states each question, the answer, the batches, and "Spend so far: 130 lookups ≈ $0.65"; `x_api_run_manifest.csv` logs every request with cost and timestamp; scripts take `--dry-run` and `--smoke` (`code/explore/explore_x_deletions.R`).

## Cases and servers

- *Sample a three-week window for event E* — program: `submit_batch.sh` over `events.yaml` → `sample_and_clean.py`.
- *Run strategies D and E for event E with bandwidth set B* — program: `run_analysis.py --config ... --event-date`, with placebo thresholds and McCrary density tests configured in YAML.
- *Try a new identification strategy* — person: A, B and C now live only in `archive_05_26/`; `estimator.py` says "Derived from follower_rkd_tweet_level.py — Strategy C pipeline stripped out".
- *Probe an external API's capabilities before spending* — mixed, and the closest thing to a card that built a server: four R scripts, a shared `x_api_utils.R`, a findings memo and a cost manifest.
- *Review a PR* — a skill wired to CI that has never fired.

Redone ad hoc: the config accumulates every past strategy (LDA with 100 topics, BERTopic, causal forest, strategies A–C) beside the live ones; `code/config/archive_05_26/` duplicates it.

## Verification

No tests, no assertions, no CI on code. Correctness rests on the study design's own checks, which are configured rather than run automatically: placebo thresholds, bandwidth sensitivity and a McCrary smoothness test flagged at p < 0.05 (`analysis_config.yaml`). `slurm/test_sample.sh` runs the pipeline on 5,000 rows "to verify everything works", a smoke test, not a correctness test. Commit `b8d26ab` vectorised the clustered standard-error routine from O(n²) to O(n log n) with no test showing equal output.

Artifact types: a cleaned panel on the cluster (script-verifiable, not verified); RKD estimates and plots (the kink coefficient has an SE, but "correct" is a level judgement about specification, and AI-on-visual could check the 68 PNGs for the diagnostics they should show); the pilot memo (AI-on-text). Unverified: whether `output/figure/2020-12-16/` came from the committed config, since the cluster copy is what ran.

## Strengths

- The pilot memo is a model: question, answer, evidence table, cost, and the code path, all in one file.
- Design choices are in YAML with comments explaining why (author FE omitted, why `log_retweets` was dropped).
- Cost-aware exploration: dry-run and smoke modes, per-request cost manifest.

## Weaknesses and limitations of the ad-hoc workflow

- **Figures not traceable**: 68 PNGs with no run id, commit or config hash; overwritten on each run.
- **Config drift**: the committed config points at cluster paths and carries dead sections; the running copy lives on Euler.
- **Superseded code kept live**: `archive_05_26/` (16 files) next to the active modules that were "derived from" them.
- **Tracked build artifacts**: 15 `.pyc` files in git; `output/table/` and `output/value/` are empty placeholders.
- **Review tooling unused**: Claude PR review installed, then bypassed by direct commits.
- **Owner invisible**: Elliott's involvement is not in the repo at all; the study's status lives in conversations elsewhere.

## What Relay would have to support here

- **Cluster runs as tracked jobs**: `sbatch` submissions with config hash, commit, log path and the output files they produced, visible from the pane over SSH.
- **Spec iteration as cards**: each strategy attempt with its rationale, result and why it was dropped, instead of an `archive_*` directory.
- **Config drift detection** between the checkout and the cluster copy.
- **Pilot memos as a template**: the `FINDINGS.md` plus cost manifest shape should be the default output of any exploratory server.
- **Diagnostics as AI-on-visual checks**: placebo and density plots reviewed against what the config says they must show.
