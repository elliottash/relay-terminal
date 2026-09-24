# tracelaw — dossier

## What it is and what we were trying to do

Tracelaw (trace.law) is "a legal document integrity auditor with evidence-linked findings, citation verification, and optional AI-assisted analysis layers" (`README.md`): a FastAPI web app with billing and OAuth (`app/`), a CLI and API, a multi-model analysis pipeline (`pipeline/`, 115 files), and a **Court Monitor** that nightly discovers opinions, analyses them, publishes pages and mails the lawyers named (`monitor/`, `deploy/`). For lawyers and readers of trace.law (`marketing/business_plan.md`). State: live in production on a Hetzner VPS plus two worker hosts (`WARP.md` "Production server").

Size (verified): 1,579 tracked files, 621 `.py`, 450 `.md`; 62 commits, one author, 2026-08-05 to 2026-09-21; 60 carry `Co-Authored-By` (34 "Oz", i.e. Codex, 23 Claude). The first commit is "Initialize Tracelaw repository", yet `backfill_fp_filter.log` is dated 2026-03-19 and `outputs/monitor/` starts 2026-03-01, so I infer a history reset and a project at least six months old. 228 entries in `tests/`, 3,189 test functions. `WARP.md` is 1,681 lines. At reading: 91 dirty paths, 43 untracked.

## How the work was actually done

- **Instruction files**: `WARP.md` is "the durable memory for facts about servers, data locations, and rules" (`CLAUDE.md`), and it is: dated incident sections ("The 8 GB claim guard stopped the backfill for 15 days"), disk tables, service names.
- **Tracker**: `issues/` (126 files) with its own convention (`issues/README.md`): `feature_requests/`, `improvements/`, `bluesky/`, subfolders `needs_qa/`, `needs_labels/`, `needs_review/`, `done/`, P1–P3 lines. Statuses: 67 open, 27 needs-qa, 11 in-progress, 11 done, 3 needs-review, 2 needs-labels. `issues/NEXT.md` (2026-09-06) lists eight "Decisions for Elliott" and a machine-work queue.
- **Relay board**: `.board/` (symlinked as `.switchboard`), created 2026-09-20 by `board_init` (`.board/survey-state.json`), one card `D7K3` in `needs-verification`, untracked. Three days old; `issues/` is the working record.
- **AI tools**: Codex sessions (`docs/codex-court-monitor-scholar-backfill-handoff.md`), Claude Code (`.board/threads/D7K3.md`), one project skill (`.claude/skills/scholar-adjudication/SKILL.md`). Runtime models GPT-5.6 Sol/Luna/Terra, Gemini, GLM-5.3-Flash (`WARP.md` "LLM API behavior").
- **Handoffs** are prose: 5 `docs/*handoff.md` and 23 dated notes in `docs/notes/`. `NEXT.md` warns "two agent sessions wrote here on 2026-09-06 — read `git status` first".
- **Long-running pipelines**: systemd units and cron in `deploy/`; logs `logs/monitor/<date>.log` (38 files). Monitoring was reading log tails until a watchdog landed 2026-09-12; before it "the worker wrote one line a minute for 15 days ... 879 jobs waited" (`WARP.md`).

## Cases and servers

- *Analyse a document* — program plus prompts (`pipeline/analyzer.py`, `prompts/type_specific/` 25 files, `prompts/meta_judge.md`): mixed algorithmic and LLM.
- *Judge a fetched Scholar opinion* — a **skill** (`.claude/skills/scholar-adjudication/SKILL.md`, "Two adjudicators, judging blind") plus machine adjudicators (`pipeline/scholar_adjudication.py`); the person rules on splits (`NEXT.md` item 1).
- *Fix stored findings after a rule change* — 31 `scripts/backfill_*.py`, 3 `scripts/reproject_*.py`, rollback tooling. One-offs that became a pattern (cohort, before-image, receipt) only by 2026-09-05 (`WARP.md` "Rules learned the hard way").
- *Rewrite KB articles* — first six tracked `patch_*.sh` (2026-03-27/28), `sed -i` edits turning bullets into prose in `articles/*.md`; four days later `scripts/rewrite_kb_articles.py` and the 3-step pipeline in `WARP.md` "KB article pipeline" replaced them. The patches were never deleted; `articles/_backup/`, `_rewritten/`, `_final/` (41/57/55 files) are three generations of the same articles.
- *Keep the server alive* — person plus runbook (`WARP.md` "What to do when the root disk fills again").
- *Decide* — the person, serialised in `NEXT.md`; nothing else serves it.

## Verification

Modes present: script (3,189 tests; `WARP.md` sequence ruff → py_compile → targeted pytest; no CI, no `.github/`); AI-on-text (false-positive filter, meta-judge, a Codex audit yielding 22 issues, `issues/2026-09-06-astra-audit-index.md`, one evidence folder `docs/qa_evidence/2026-09-06-astra-audit/`); metric with frozen protocol (`docs/benchmarks/2026-08-27-light-monitor-pipeline-benchmark.md`); human legal review of 20 opinions (`docs/benchmarks/2026-08-27-light-document-defect-manual-review.md`).

Fit by artifact: pipeline code → script. Classified corpus → metric against gold, but "`human_annotations` is empty; GLM calls 52% of edges positive where Gemini called 12%" (`issues/PRIORITIES.md`). Deployed site → human or AI-on-visual; 27 cards in `needs_qa/`, "no person has opened one". Adjudication → blind pairwise human; "Elliott judged 3" of 50 (`NEXT.md` item 7).

Unverified, and the cost: the 2026-03-19 backfill (`backfill_fp_filter.log`) removed 872 of 6,384 published issues across 915 sessions by model, "0 errors", no human sample; the only record is a nohup log at the repo root. Until 2026-09-05 "every one of the 2,538 inconsistency findings ever published ... rested on the model's memory of the cited case" (`WARP.md`); residue: 20 flags "each a false accusation about a named lawyer's work" (`PRIORITIES.md`).

## Strengths

- `WARP.md` turns incidents into dated rules with the measurement attached.
- The tracker separates QA, labels and review as three human questions (`issues/README.md`); `NEXT.md` separates owner decisions from machine work.
- Re-projection with before-images and receipts (`scripts/reproject_*.py`).
- Frozen benchmarks with a stated decision; a real skill for the judgement no check can make.

## Weaknesses and limitations of the ad-hoc workflow

- **Two trackers**: `issues/` (126 files) and `.board/` (1 card); `NEXT.md` is 17 days stale.
- **Uncommitted production**: 43 untracked paths including `deploy/*.service`; a cloud review called the monitor "dead because ... untracked" (`WARP.md`). Code reaches production by Mutagen one-way sync, so uncommitted edits run live (`docs/deploy-hetzner.md`).
- **Duplicated one-offs**: 6 `patch_*.sh`, 31 backfills, `app/templates/_backup_2026-04-05/main.py`, three article generations.
- **Stale docs**: `planning/plan.md` says the FP filter is "GPT-5.5-powered"; the module says GPT-5.6 Sol; the log says GPT-5.4. `app/main.py:5578` sets `fp_filter_applied` from `gemini_analysis is not None` (inferred mismatch).
- **Strays**: empty `nohup.out` and `trace_law.db`, a file named `=`, `_archive/` at 3.4 GB.
- **Credentials**: `.env` is mode 600 and untracked (not opened); but the SES key "was printed into a transcript on 2026-08-16 and still sends" (`NEXT.md`).
- **Human bottleneck**: eight rulings queued for one person; 22 audit findings "all open".

## What Relay would have to support here

- **Decision cards**: `NEXT.md`'s rulings are cases only the owner serves; they need a dated queue with the packet attached (the SerpAPI spend "before 29 September").
- **Labels and review as lanes distinct from QA**, as `issues/README.md` defines them; the board has only `needs-qa`/`needs-verification`.
- **Long-running jobs on the card**: unit, log tail, held/progress state, so "one line a minute for 15 days" is seen.
- **Re-projection as a server**: cohort → dry run → before-image → apply → receipt, with a human sample required.
- **Promote one-offs**: detect `patch_*.sh`/`backfill_*.py` families and fold them into a script or skill.
- **Memory from state**: `WARP.md` "keep current" sections regenerated, not typed.
- **Deploy gate**: warn when the synced production tree differs from `main`.
