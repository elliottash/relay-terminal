# wondernauts — dossier

## What it is and what we were trying to do

Wondernauts is "a Godot 4 local-co-op 2D action platformer with short Mario-style levels, Wonder-grade movement, and AI-generated thematic variation" (`README.md`). Ship scope was decided 2026-08-07: the W1+W2 campaign, Custom Game, PvP Arena and CounterSiege; everything else "out of ship scope by decision, not by neglect" (`WARP.md`). The release push is a Steam store page, a Next Fest demo, then ship (`issues/README.md`). Domain: game development, which here means code, procedural generation, generated art, synthesised audio, and playtesting with the family on a Steam Deck.

Size (verified): 1,965 commits between 2026-06-25 and 2026-09-18; 1,942 authored as Elliott Ash, 19 as "GLM 5.3 (OpenCode)" and 4 as "DeepSeek V4.1 Flash (OpenCode)"; 1,353 carry `Co-Authored-By`, the leading trailers being Claude Fable 5 (637), Claude Opus 4.8 (405), "Oz" (159) and Claude Opus 5 (89). Working tree 36 GB (`assets/` 18 GB, `.git` 16 GB); `docs/` has 11,630 files, 853 of them under 53 dated `docs/handoffs/` folders; `tools/` has 1,702 entries; tests are 394 Python files (2,354 test functions) and 1,189 GDScript files. Evidence outside the repo: `~/data/wondernauts_archive/`, 272 entries, 241 GB. `WARP.md` is 1,381 lines; `CLAUDE.md` and `AGENTS.md` are one line each pointing at it.

## How the work was actually done

- **Routing documents**: `WARP.md` (rules, gates, guardrails), `SEARCH.md` (which path owns which subsystem), `docs/STATUS.md` ("the single status and routing authority", newest-first dated paragraphs), a 21-item design doc stack in `docs/design/`. `WARP.md` opens with "Current direction — September 12, 2026 (supersedes v2 rebuild guidance)": a v2 rebuild was attempted, then demoted to "a preserved donor" and v1 restored.
- **Board**: `issues/`, adopted 2026-08-21 from the owner's `issue-tracking` skill. Category folders (bugs 45, content 51, procgen 46, process 36, steam 11) and lifecycle folders named for the kind of verification owed: `needs_qa_llm/` 32, `needs_qa_human/` 90 ("Elliott's queue"), `needs_review/` 5, `needs_ab/` 1, `needs_labels/` 0, `done/` 49. `RANKING.md` is the queue. Intake arrives as `issues/bug_intake.txt` (empty now), `issues/playtest_triage.txt` (34 lines) and two `.docx` files (`playtest-2026-08-24.docx`, `bug-clarifications.docx`).
- **AI tools**: 27 Codex skills in `.codex/skills/` (`create-enemy`, `sprite-improver`, `steam-deck-deploy`, `enemy-ensemble-designer`, …) with a shared `CONVENTIONS.md`; two Claude skills in `.claude/skills/`; GLM batch classification of 24,408 enemy pairs run under a systemd supervisor (`docs/STATUS.md`); "Warp Oz child agents" for classification; Modal for QA sweeps on the laptop, local on `spark` (`WARP.md` "Where QA sweeps run"). `.claude/settings.local.json` allow-lists individual Godot test commands.
- **Scripted vs manual**: nine launchers (`start.sh`, `pvp.sh`, `gauntlet.sh`, `custom-*.sh`, …); capture, audit and generation tools under `tools/`; four GitHub workflows, one pushing an orphan branch because "the game tree is ~25 GB" (`.github/workflows/native-extensions.yml`). Manual: every Deck session, art pick and design decision.
- **State between sessions**: `docs/STATUS.md` plus a dated handoff folder per piece of work; Mutagen sync laptop to `spark` (`mutagen.yml`) and a Syncthing marker (`.stfolder`).

## Cases and servers

- *Add a level*: a folder contract (`data/biomes/<biome>/levels/<key>/style.json` + `ideas.json`) guarded by `tests/generation/test_biome_contract_consistency.py`, built after "157 files were silently dead one way and 13 the other" (`README.md`).
- *Export a build to the Deck*: `WARP.md` "Steam Deck export and deploy" plus the `steam-deck-deploy` skill; a program plus a skill.
- *Generate or repair a sprite*: the `sprite-improver` skill over `tools/art_pipeline`, with contact sheets as the review surface.
- *Review concept art*: the person. `concept_art/controllers/review_status.json` records `review_state` (pending, picked, working_direction) and notes such as "User prefers v1 over v2"; `concept_art/controllers/_selected/_README.md` lists the chosen reference per boss with its source path. Prompts sit beside images (`concept_art/**/prompt.txt`).
- *Triage a playtest*: `WARP.md` "Bug intake workflow" → `docs/QA/current/bug_repro_ledger.md` → an issue; the owner's rule of 2026-09-16 that an item not re-reported counts as fixed closed two intakes (`docs/STATUS.md`).
- **Redone ad hoc**: dozens of `tools/_probe_*.gd` and `_capture_*.gd` one-offs, which `WARP.md` now forbids ("Do not invent a bespoke screenshot/camera/collage script"); two archived worktrees consolidated 2026-09-13 (`issues/2026-09-13-flight-link-host-entry-orphaned.md`); the 2026-08-07 reset "archived 21 loose notes files"; the v2 rebuild abandoned.

## Verification

Modes present, all of them: **script** (pytest and GDScript suites; `data/qa/test_manifest.json` holds one `last_verdict` per test and `tools/test_verdict_log.py` appends run history outside the repo); **metric gates** with ceilings (traversability, containment, enemy pressure; "regression ceilings, not targets"); **AI-on-visual**, advisory by rule ("AI or vision review is advisory unless a skill explicitly names it as the accepted judge… It never overrides deterministic hard gates", `.codex/skills/CONVENTIONS.md`); **AI-on-text** as the `needs_qa_llm` lane; **levels and pairwise** for art (`review_status.json`); **human playtest** as `needs_qa_human` and `docs/QA/manual/manual_playtest_queue.md`, whose rows carry "Human Pass Criteria" written in prose.

What is unverified is said plainly: "Exit status is not evidence. 716 of ~820 GDScript tests use that harness" (`WARP.md`); "25 passing local Python/wrapper checks, but 0/42 runtime witnesses: deployment acceptance remains unproven" (`docs/STATUS.md`); "Modal cannot see local Steam Deck evidence". `issues/CHECKLIST-elliott-store-page.md` names the four-mode Deck playtest "the single most valuable missing artifact". Cost on record: 170 level files silently ignored by a loader that chose by spelling.

## Strengths

- The lifecycle folders map one-to-one onto verification kinds, so the queue itself says what proof is owed and by whom.
- A severity ladder, a plateau rule and the "AI review is advisory" rule are written once and shared by 27 skills.
- Evidence is retained with provenance and seeds ("exact seeds are reproduction evidence, not automatically the durable test").
- Human pass criteria are written before the human plays.
- Stale-evidence and status-header rules exist because the tree went stale before, and they say so.

## Weaknesses and limitations of the ad-hoc workflow

- **One human, 90 items**: `needs_qa_human/` is the bottleneck and every "TECHNICALLY COMPLETE, awaiting human QA" line in `docs/STATUS.md` waits on the same Deck.
- **Volume**: 36 GB plus 241 GB of archive; CI cannot check the tree out; `WARP.md` at 1,381 lines with six "CRITICAL GUARDRAIL" sections and three direction changes in one September.
- **Art provenance leaks out of the repo**: `_selected/_README.md` points at `/home/elliott/.codex/generated_images/…` and at `artifacts/_archive/pre_2026-05-08/…`; `review_status.json` was last updated 2026-04-26 while later picks live only in the README.
- **Decisions in prose**: art picks, playtest verdicts and scope rulings are sentences in `STATUS.md`, `RANKING.md` and handoffs, not fields.
- **Intake in `.docx` and `.txt`**, triaged by hand into Markdown.
- **Tooling sprawl**: 1,702 entries in `tools/`, many single-use probes the rules now forbid.
- **Three sync mechanisms** (Mutagen, Syncthing, git) across two machines, and `.env` with API keys in the tree (mode 0600, not opened).

## What Relay would have to support here

- **A human-QA pane**: the `needs_qa_human` row as a card with device, entry point, pass criteria and a place to dump notes from the Deck, so the intake is not a `.docx`.
- **Judgement objects for art**: pairwise and level verdicts on images, with the prompt, generator and chosen file as provenance, replacing `review_status.json` plus a README.
- **Typed metric verifications** with a denominator ("0/1,128 levels", "0/42 witnesses"), and the hard-gate versus advisory distinction from `CONVENTIONS.md` built in.
- **Evidence store outside git, linked from the card**: the `~/data/wondernauts_archive` pattern with an index Relay can open.
- **Skills as servers with conventions**: plateau, severity ladder, "cite the wrapper", shared across Codex and Claude sessions.
- **Remote compute as hosts**: Modal, `spark` and `filly` each with rules about what evidence they can and cannot see.
- **Big-tree awareness**: no full-tree search by default, orphan-branch CI, and a status document that is generated from cards rather than prepended by hand.
