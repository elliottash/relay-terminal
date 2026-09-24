# modalities — dossier

## What it is and what we were trying to do

PRISM: "measures constitutional reasoning in Supreme Court opinions" (`README.md`). CourtListener opinions and SCDB metadata go through a 31-stage pipeline (`court-listener/01_download` … `21_extract_relations`, `30_combine_prism`, `31_clean_prism`) in which LLMs code each opinion for modalities of argument, then into immutable data releases, an R package of measures (`analyze/prismtools/`), Quarto reports and LaTeX/Word manuscripts for up to nine papers (`analyze/01_paper1_landscape` … `09_paper9_spatial`). The repo is `BubbLab/modalities`; Ryan Bubb has 812 of 825 commits, Elliott Ash 12. Elliott's role is Paper 1 ("i take over paper 1", `docs/elliott-notes/prism-trello-card.md`) and planning notes in `docs/elliott-notes/`.

State: very active; release v0.1 finalized 2026-09-09 (`court-listener/releases.lock.json`); Paper 1 being drafted. Size (verified): 825 commits, 2025-10-23 to 2026-09-23, with 255 in June and 259 in September 2026; 616 commits carry a Claude `Co-Authored-By` line (Opus 4.8: 211, Opus 4.7: 76, Fable 5.1: 54). 3,508 tracked files: 1,579 `.json`, 445 `.md`, 359 `.py`, 92 `.R`, 59 `.tex`, 39 `.qmd`. Working tree clean at reading. Python 3.12 via `uv`, R via `rig`/`rv`, Make, Quarto, LaTeX, Word.

## How the work was actually done

This is the most process-heavy of the four, and the process is written down:

- **Instruction files**: `CLAUDE.md` (9.7 KB; `AGENTS.md` is a symlink to it, same in `analyze/`), `analyze/CLAUDE.md`, `court-listener/CLAUDE.md`, plus per-module READMEs. They carry rules like "Never repair a released dataset, cached bundle or generated manuscript fragment by hand".
- **Board equivalent**: `docs/plans/06_implementation_roadmap.md` assigns tasks R1–R4, J1–J3, M0–M2, P1, W1 to agents; each owner keeps an append-only `docs/plans/implementation-log/<ID>.md` (33 entries, 682 files with evidence directories). `docs/plans/agent-prompts/` holds the four prompts that launched agents (`W1.md`, `R4-shark.md`, `R4-laptop.md`, `J3-identity-audit.md`).
- **Concurrency**: several agents in one checkout, serialized by `/tmp/modalities-git-mutations.lock` and per-area ownership locks (`/tmp/modalities-paper2-ownership.lock`, `docs/plans/implementation-log/W1.md`). Three machines: `shark` (sole writer of the private archive), a laptop, a Mac (`README.md`, `W1.md`).
- **Runs**: paid stages 07–21 require a `run.json` identity and refuse over 1,000 clusters without `CONFIRM_LARGE_RUN=1` (`court-listener/CLAUDE.md`). Prompts are versioned by fingerprint (`21_extract_relations/code/instrument_lock.json`, version 0.9.17).
- **Elliott's own sessions**: `docs/elliott-notes/` was prepared 2026-09-06 from Dropbox, email and SMS excerpts with an "independent Claude Fable CLI pass" (`overnight-plan.md`); `reference-inputs/dropbox` symlinks to a dated snapshot.

## Cases and servers

- *Code cluster C with instrument version V* — mixed: the prompt is the skill (`10_extract_modalities/code/modalities_prompt.py`), the driver the program (`extract_modalities.py`, Make targets).
- *Judge run A against run B* — AI pairwise: `judge_modalities.py`, `judge_relations.py` (`--mode correct|adjudicate`) over a shared `common/judge`; human adjudication fixtures in `21_extract_relations/hand/revision_loop/`.
- *Verify a release copy before computing* — program: `PRISM_RUN` wrapper checks every file against `releases.lock.json` (`analyze/CLAUDE.md`).
- *Compute Paper 1 results and build the PDF* — program: `make -C analyze/01_paper1_landscape build`; `pdf` reuses results and "rejects stale analysis sources".
- *Build the Paper 2 Word packet* — program (`make ... bundle-docx`), with the Word master kept authoritative and "no automatic Word updater" (`agent-prompts/W1.md`).

Cards that built servers: R1 release tooling, M2 registry, P1 build system, W1 Word packet. Redone ad hoc: June and August production runs were stamped after the fact ("timestamp-assignment caveat", `court-listener/CLAUDE.md`); Paper 2's August 31 estimates were preserved by receipt (`W1-evidence/august31-retention.json`).

## Verification

Modes present, and unusually separated: byte identity (release and analysis sha256 in `releases.lock.json`, bundle locks), fixture tests (166 `test_*` files: 107 under `analyze/`, 50 under `court-listener/`), instrument fingerprints, AI pairwise judgement between runs, human adjudication fixtures, and human research review. The roadmap insists these are different things: "Do not count a fixture test as completion of a required real-data or remote test"; "Passing a submission check does not establish scientific validity or human approval" (`README.md`). Coded values are ordinal levels (depth, centrality, direction, `docs/elliott-notes/technical-handoff.md`), so the coded dataset is verified by pairwise LLM judgement plus hand fixtures, not by a metric. No CI: there is no `.github/`.

The cost of earlier gaps is visible: an inspected build summary had empty "classification and modality version strings" (`technical-handoff.md`), which is why provenance stamping was retrofitted; and the W1 human gates were later removed by the coauthor (`agent-prompts/W1.md`).

## Strengths

- Provenance is a machine contract: run identity, instrument fingerprint, release hash, bundle lock, each refusing silent drift.
- Measures live in one tested package (`prismtools`), and reports may not reimplement them.
- Task logs separate "implementation finished" from "task complete".
- Elliott's notes tag each claim as inspected, emailed or proposed (`technical-handoff.md`).

## Weaknesses and limitations of the ad-hoc workflow

- **Process weight**: 445 Markdown files; an agent must read four instruction files and a roadmap before touching a paper. Gate rules churn (W1 above).
- **Legacy paths coexist**: `analyze/input` still symlinks to live `31_clean_prism/output`; papers 03–09 use the old `code/paperN.qmd` layout (`analyze/Makefile`), against the rule that new work reads releases only.
- **Stage renumbering**: what was stage 21 combine is now stage 30 ("Do not assume that old stage numbers or symlinks still match", `technical-handoff.md`).
- **Session state outside git**: ownership and coordination in `/tmp/*.lock` and `/tmp/modalities-w1-coordination.md`; archive only on shark.

## What Relay would have to support here

- **Task logs as cards** with the roadmap's fields: scope, commits, validation by kind (fixture / real data / remote / human), omitted checks, successors unblocked.
- **Multi-machine, multi-agent ownership** as data, not `/tmp` locks and prose notices.
- **Pairwise AI judgement as a built-in verifier** for coded datasets, with the human adjudication fixtures as the ground truth it is scored against.
- **Instrument versioning for prompts**: the skill that codes an opinion has a fingerprint and a changelog; Relay skills should too.
- **Coauthor-facing surfaces**: the Word master and a Trello card are where the collaborator lives; the board must read and write those.
