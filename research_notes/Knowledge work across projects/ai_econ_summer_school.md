# ai_econ_summer_school — dossier

## What it is and what we were trying to do

`/home/elliott/teaching/2026-08-Zurich-Summer-School/ai_econ_summer_school` is the instructor repository for a summer school taught over Zoom on 31 Aug to 3 Sep 2026 and in person the week after, by five instructors (`AGENTS.md`). Domain: teaching. It is "content-first ... not a software application. There is no build system, test suite, or deployable code" (`AGENTS.md`). Git, remote on a co-instructor's GitHub (`git remote -v`): 54 commits from 2026-07-28 to 2026-09-02, 36 by Andrea Ciccarone, 13 by Elliott Ash, 5 by Fabian Roeben; 4 commits carry a `Co-Authored-By` line. 2,638 tracked files, mostly figures (1,231 png, 486 eps, 442 pdf, 187 tex, 41 ipynb); `sources/` is 2.7 GB of prior-course material, `materials/` 192 MB.

The aim was to produce nine lecture decks, four notebooks and a problem set from older courses, publish student-facing PDFs to a public site, and keep instructor material private.

## How the work was actually done

- **Instruction file**: `AGENTS.md` (`WARP.md` is a symlink to it): layout, the `NN_topic_instructor` folder rule, "don't commit or push unless explicitly asked", the hard privacy rule, and the publishing pipeline to `~/repos/zrh_ai_econ` and a Hetzner host, with the full runbook at `~/admin/hetzner/zrh-ai-econ-course-site.md` (verified present, 125 lines).
- **Ledger in the README**: `README.md` is 383 lines, of which "Recent activity" holds 114 dated entries (10 on 08-28, 51 on 08-29, 26 on 08-30, 25 on 08-31, 2 on 09-02), a "Decisions" section with one-line rationales ("Do not prescribe a specific embedding model in the assignment"), and "TODOs / deadlines" with four boxes still open, all rehearsal or final review. The ledger convention is not described in `AGENTS.md`; it is inferred to be an agent habit that stuck.
- **AI tools**: the entries read like an agent's turn summaries ("Rebuilt and visually checked the 51-slide deck"; 65 occurrences of "visually checked"). Which tool is not recorded; the four co-authored commits are the only trace in git.
- **Scripted**: `publish_slides.sh` compiles a handout build with `\PassOptionsToClass{handout}{beamer}`, strips intermediates, copies to the public repo and prints page counts; `materials/notebooks/build_notebooks.py`, `02_.../make_figures.py`, `problem_set_andrea/build_dataset.py`. **Manual**: every slide edit, the rehearsals, commit and push, and `deploy.sh` in the other repo.
- **State between sessions**: the README ledger, per-topic `README.md` files (present in 6 of 9 topic folders; `05_llms_alignment_elliott/README.md` still says "_TODO: enumerate 3–4 concrete learning goals_"), and git. `NOTES.md` is git-ignored scratch and is absent; `tmp/` is empty.

## Cases and servers

- *Edit a slide, rebuild, look at it, log it* — the dominant case, repeated 114 times in four days. Served by a person driving an agent with no skill file; the loop is implicit in the ledger's shape.
- *Publish a deck* — a program, `publish_slides.sh`, built after the manual five-step recipe in `AGENTS.md`. Its `DECKS` table is one of three copies of the name mapping: the script lists lectures 01 to 07, `AGENTS.md` lists 00, 01, 02, 03, 05, 08, and the authoritative table is said to be `~/repos/zrh_ai_econ/slides/README.md` (verified discrepancy).
- *Adapt a lecture from `sources/`* — a per-topic README records "Sources Adapted" and "Coordination Notes" between instructors; no server, redone per topic.
- *Build notebooks and the dataset* — programs, run once each.
- *Write a presenter script with timings* — done for three lectures on 08-30 and 08-31 (README entries); a skill-shaped task with no skill.
- *Deploy the site* — a program in the other repo with checksum dry-run and live verification.

## Verification

Artifacts are PDFs, notebooks and a dataset. Modes present: **script** for compile success (`pdflatex -halt-on-error`, two passes), page count via `pdfinfo`, and byte identity live versus local (`sha256sum` against `curl`, in `AGENTS.md`); **AI-on-visual** for slide layout ("Rebuilt and visually checked"), claimed in the ledger but with no image evidence kept; **person** for pedagogy, through rehearsal, which the TODO list shows was still pending on the course's first day ("Rehearse the 90-minute lecture once"). Presenter timings (89 and 88 minutes for 43 and 51 slides) are model estimates, not measured. Notebooks have no execution check recorded. Nothing verifies that student and solution PDFs differ only in the hidden code blocks, which is the one privacy-critical property.

## Strengths

- A clear, dated decision ledger with reasons, which most research repos lack.
- The privacy rule is explicit, repeated in three places, and enforced by a separate public repo rather than by care.
- Publishing became a script after being a recipe, with page counts and hash checks.
- Topic READMEs record provenance from `sources/`, so adaptation is traceable.

## Weaknesses and limitations of the ad-hoc workflow

- **Working tree drift**: `git status` shows 10 entries: two modified handout PDFs, six untracked public-name PDFs (`02-machine-learning-basics.pdf` and peers, produced by `publish_slides.sh` but not ignored or committed), and two deletions in `08_training_llms_felix/` including a file named `... guide (3).pptx`.
- **Sync conflicts inside git**: `materials/05_llms_alignment_elliott/slides (Elliott Ash's conflicted copy).out` and `... copy 1).out`, from a Dropbox-and-git overlap.
- **Build leftovers**: `03_text_image_data_andrea/` holds `03-review.aux/.log/.nav/.out/.snm/.toc/.vrb` from a jobname nobody documented, beside `main.*`.
- **Mapping in three places** (script, `AGENTS.md`, public README) that already disagree.
- **Ledger without evidence**: 65 "visually checked" claims and no screenshot; timings that were never measured.
- **Rehearsal never closed**: the four open TODOs are the only human verification, and the course started with them open.
- **Unfinished scaffolding**: two Week 1 slots and all of Week 2 "have no folders here yet" (`materials/README.md`); one topic README keeps a placeholder for learning objectives.

## What Relay would have to support here

- **An edit-build-look loop as a named server**: rebuild, render the changed pages, diff them visually, and attach the image to the ledger entry instead of the words "visually checked".
- **A decision ledger as a Board object**: dated decision, reason, affected deck, generated from the session rather than typed into `README.md`.
- **Publish as a card with a privacy gate**: the student/solution split checked by diff before anything is staged; the name mapping held once.
- **Multi-author awareness**: three instructors committing to one repo with Dropbox underneath; conflicted copies and untracked outputs should be surfaced at session start.
- **Person-only verification tracked honestly**: rehearsal and timing are things only the lecturer can do; the card should say "unverified until rehearsed" rather than let a model estimate stand in.
- **Pairwise judgement on slides**: old versus new page rendered side by side is the natural review for a deck, and the only one that fits an artifact with no metric.
