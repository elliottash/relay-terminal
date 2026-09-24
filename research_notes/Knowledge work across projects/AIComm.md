# AIComm — dossier

## What it is and what we were trying to do

A survey experiment on AI-assisted opinion writing: "Sharing Opinions in the Shadow of AI", Ash, Lou, and Song (2026) (`analysis/code/main_replication.do`). Participants recruited through Instagram in Zurich wrote posts with or without AI; outcomes include completion, post meaningfulness, text length and willingness to pay (`README.md`). The repo holds "the full research workflow": raw Qualtrics exports, a Stata cleaning pipeline, the full analysis and a curated replication package (`README.md`). GitHub `lenasong/AIComm`; a team project with a research assistant doing most commits.

State: paper-stage, replication package assembled; last commit 2026-06-04. Size (verified): 225 commits, 2024-04-25 to 2026-06-04, in bursts (51 in May 2024, 17 in Dec 2024, 16 in May 2025). Authors: sahiladane 112, Amin Rahmati 60, lenasong 28, Elliott Ash 18, Noah Fehr 6, yclou8 1. Zero `Co-Authored-By` lines. 788 tracked files: 549 `.pdf`, 45 `.png`, 41 `.csv`, 38 `.tex`, 30 `.do`, 9 `.dta`, 8 `.lyx`, 7 `.py`. Stata 17 MP, Python with `gslab_make`, LyX, GPT-4o-mini for text scoring.

## How the work was actually done

- **Template**: "the broad structure of the `gentzkow/template_archive` style" (`README.md`): `config.yaml`, `config_user.yaml`, module `make.py` scripts, `input.txt` link files, and two root entry points `run_all.py` and `run_replication.py`. The template's logs are committed (`analysis/log/make.log`, 2026-05-31, from a `D:\` path).
- **Board**: GitHub issues, threaded through commit messages: 53 commits read `Refs #NN — <issue comment URL>` (issue #17: 23 commits, #16: 12, #19: 5). The issue thread is where a figure is posted, commented on and revised: "Addressing the comments after the last commit", "Winsorized at the 0-95t instead of 1-99", "F-statistics instead of p-value for balanced table". `.claude/` is gitignored, so an AI session existed locally but left no trace beyond a commit titled "AI-generated document" (`a9d52215`).
- **AI in the data**: GPT scores are "pre-computed offline" by `data/gpt_annotations/New_GPT_Measurements.py` (gpt-4o-mini, API key placeholder) and "not part of the `run_all.py` chain"; `llm_model_comparison.ipynb` compared models. Pangram AI-detection scores arrive as a `.dta`. `helpers/table1A_report/Report_Claude.md` and `Report_Gemeni.docx` are AI-written compilations of Swiss population statistics for the balance table.
- **Handoff**: `helpers/Documentation.md`, "every file that runs, what it reads, what it does, and what it writes", is the only onboarding document.
- **Runs are not named**: every `make` "first clears its output and log folders" (`Documentation.md`), so there is one current output set; history is in git and in versioned slide PDFs (`paper_slides/output/AIComm_Slides-2024-05-12-v1.pdf` … `v5.pdf`).
- **Manual**: raw exports re-downloaded by hand (`data/raw/archive/` holds nine dated CSVs); a Windows Stata path committed in `config_user.yaml` despite its header "should not be committed".

## Cases and servers

- *Rebuild every figure and table after a spec change* — program: `run_all.py` runs four do-files through `sub_programs.do` and writes 426 PDFs under `analysis/output/`.
- *Rebuild exactly the paper's exhibits* — program, and the one card that clearly built a server: `run_replication.py` → `main_replication.do`, with a README table mapping Figure 1 … Table A3 to files.
- *Score free-text posts with an LLM* — mixed: `New_GPT_Measurements.py` is a program wrapping a prompt; run once, by hand, outside the chain.
- *Revise Table 1 / make the CI plots nicer* — person plus issue thread, iterated over many commits (issues #16, #19).

Redone ad hoc: cleaning exists twice (`data_cleaning.do`, `data_cleaning_ff.do` "repeats the same cleaning"), the program library twice (`sub_programs.do`, `sub_programs_replication.do`), treatment effects twice (`treatment_effect.do`, `treatment_effect_ff.do`); an imputation branch was abandoned into `analysis/code/archive/`.

## Verification

Nothing automated: no tests, no CI, no assertions. `make.log` even warns "target files have been modified according to git status" for both cleaned datasets, and the run proceeded. Correctness was established by people looking at PDFs in issue threads, and by the pre-analysis plan (`paper_slides/output/AIComms_Swiss_PAP.pdf`) as the reference for what should be estimated.

Fitting modes: the cleaned dataset (n = 1,075 / 929) could be checked by script against the codebook; the treatment-effect tables and CI plots have no verifiable metric, only levels (right specification, sample, winsorization), where AI-on-visual catches label and axis errors and pairwise old-versus-new fits the issue-thread loop; the LLM text scores were checked only by a one-off model-comparison notebook. Sample, winsorization and balance-test choices each changed in separate commits; only the final state is reproducible.

## Strengths

- One command reproduces the paper's exhibits, with a file-to-figure inventory (`README.md`, `main_replication.do` header).
- Raw inputs, codebook and cleaned data are all in the repo, so a clone runs (given Stata).

## Weaknesses and limitations of the ad-hoc workflow

- **Outputs in git without provenance**: 549 PDFs, `data/output/*.dta` and `analysis/input/*.dta` committed; no run id links a PDF to the commit that made it.
- **Duplicated scripts** (`*_ff.do`, `sub_programs*.do`) that must be edited in pairs.
- **LLM scoring outside the pipeline**: the GPT `.dta` is an opaque input; the prompt, model version and date are only in a script with a redacted key.
- **Machine-specific**: PowerShell instructions, `D:\` paths, a `.bat` converter, a committed Stata path.
- **Stale strata**: `analysis/code/archive/`, `Archive.do`, `paper_slides/code/AIComm_Slides.lyx~`, `.aux`/`.log` files under `paper_slides/input/`, nine superseded raw exports.
- **AI reports as sources**: `Report_Claude.md` computes Swiss population means from interpolated tables; its numbers feed a balance table with no check.

## What Relay would have to support here

- **Issue thread as the card**: the `Refs #17 — <comment URL>` convention is a hand-built link from commit to review; Relay should make the exhibit, the comment and the commit one object.
- **Exhibit diffing**: pairwise old-versus-new PDF comparison with an AI-on-visual pass for labels, axes and sample sizes.
- **Pipeline-external steps as recorded servers**: the GPT scoring run needs a prompt version, model, date and cost attached to its output file.
- **Spec-change ledger**: winsorization, sample and test choices as tracked decisions, not commit messages.
