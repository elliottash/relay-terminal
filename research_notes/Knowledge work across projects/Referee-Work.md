# Referee-Work — dossier

## What it is and what we were trying to do

`/home/elliott/admin/Referee-Work` is the owner's working folder for peer review in every role: referee for economics journals, program-committee reviewer for NLP conferences, grant and prize assessor, guest editor of two special issues, editor at the Journal of Law and Economics (JLE), and area chair on OpenReview. Not git; the skills that operate on it address it as `~/Dropbox/_Ash_Admin/Referee-Work/` (`jle-editing/SKILL.md`). Verified size: 14 top-level entries; `_done/` holds 43 case folders from 2026 plus yearly zips `2015.zip` to `2025.zip` indexed by `_done/_archive_catalog.csv` (1,551 rows with path, size, mtime, sha256). Cases per year from that catalogue rise from 1 in 2015 to 65, 75 and 71 in 2023 to 2025.

The goal was never to build a system. It was to get each report written well and on time. A system grew anyway, unevenly: a one-paragraph prompt in 2025, then skills, then a scripted editorial office for JLE.

## How the work was actually done

- **Case folders** are named `<due-date>-<venue>[-<slug>]`; the date is the deadline, since `2026-09-25-AEJEP` was created on Aug 31 (mtime, verified). A live case holds `paper.pdf`, `paper.txt`, `referee-review-support.md` (57 KB in `2026-09-18-EG`), `referee-report-draft.md` and the final `referee-report.txt` (2 KB). Revision cases add the prior decision, prior reports and author replies as named PDFs.
- **Prompts**: `prompt.txt` and `prompt-03.txt` are byte-identical (verified by `diff`), dated 2025-05-16: a six-sentence summary plus ranked "Potential Problems". They are the pre-skill server. `report-advice-from-joachim.txt` is an editor's format spec (three sections, two pages, six weeks). `JEP - how to write a referee report.pdf` (2017) is the canonical reading.
- **AI tools**: Warp/Claude skills under `/home/elliott/Dropbox/_Agents/.warp/skills/`; browser automation for Editorial Manager in `JLE-Editing/code/` (27 `.py` scripts, e.g. `em_submit_decision.py`, `em_invite_reviewers.py`, `em_proxy_register_reviewer.py`); the OpenReview API in `OpenReview/scripts/`; Gmail fetching (`fetch_jle_emails.py`).
- **State between cases**: for refereeing, the folder itself and its move to `_done/`. For JLE, `JLE-Editing/JLE-tracker.md` (123 lines, "Last refresh: 2026-08-19") and a queue of directories: `_pending` (12), `action-required` (3), `waiting-on-others` (2), `_active/under-review-0..3` (all empty), `_done/` by outcome (desk-rejected 56, re-assigned 13, rejected-with-reports 7, RR 6, referred-for-DR 6, accepted 0). `JLE-Editing/WARP.md` is the runbook and incident log: a duplicate manuscript directory "hijacked" a decision submission, so `dedupe_submissions.py` now enforces "exactly one live directory per ms_num".
- **Guest issues**: `EJ-Guest-Issue/` uses numbered stage folders (`0 - DR`, `4 - review`, `5 - decision time`, `6 - RR`), five scripts `_code/01_extract_and_summarize.py` to `05_download_single_report.py`, `scores.xlsx`, and `_review-notes.txt` with one line of desk-reject reasoning per manuscript. `SJES-Guest-Issue/` is submissions only.

## Cases and servers

| case | how often | served by |
|---|---|---|
| Referee report for a journal | most of 43 in `_done/` | skill `referee-review-support` (262 lines) produces the support doc; the report itself is the person |
| Revision round (R&R as referee) | `2026-07-23-Restud-RR` and others | same skill; `references/revision-comparison.md` |
| Conference PC review | ACL, COLM, TACL, NLP-CSS, SwissText folders | no skill; `conference-paper-eval` targets the Zurich workshop only |
| Grant / prize / LOI review | ERC, junior-scholars, Arnold-LOI folders | no skill; per-paper `*-referee-review-support.md` plus `.okular` annotations |
| Desk reject (editor) | 56 JLE cases | skills `editing-desk-reject`, `jle-select-coeditor`, `jle-confer-editor`; script `em_complete_dr.py` |
| Reject with reports / R&R (editor) | 7 and 6 JLE cases | `editor-support-dossier`, `editing-reject-with-reports`, `editing-rr`; `em_submit_decision.py` |
| Send for review (editor) | ongoing | `editing-suggest-referees`, `editing-send-for-review`, `em_invite_reviewers.py` |
| Whole JLE inbox refresh | per session | skill `jle-editing` (333 lines) plus the pipeline in the tracker header |
| Guest-issue scoring | one issue | `EJ-Guest-Issue/_code/*.py`, no skill |
| Area chair | NeurIPS and EMNLP 2026 | skill `openreview-ac` (reminders); `OpenReview/scripts/` (rankings, forum audit, recommendation posting) |
| Accept decision | 0 cases | nothing |

Adoption is partial: 13 of 43 `_done/` folders contain `referee-review-support.md` and 3 a `referee-report-draft.md`, while 18 contain a bare `report.txt`. The support doc is the server; the final report remains handwritten from it.

## Verification

The artifact is prose with a recommendation: no script can check it, and there is no ground truth. Quality was judged by the person reading the support document against the paper, then by whether the editor's later letter agreed, which is not recorded anywhere (only 3 files in `_done/` match "decision" or "outcome"). The fitting modes are **levels** (a recommendation scale) and **pairwise** (this report against the other referees', visible only in R&R rounds where prior reports arrive as files).

Where verification exists it is on the servers, not the reports: `editor-support-dossier/evals/` holds three eval cases and a `review.html` (the only skill with stored evals); `JLE-Editing/_active/screenshots/` has 117 screenshots, the AI-on-visual trace of browser automation; `conference-paper-eval` emits calibrated scores, a levels artifact. `OpenReview/AC-MASTER-INDEX.md` records an integrity check across assigned papers (a hidden prompt injection detected in most of them), which is AI-on-text verification of the inputs rather than the outputs.

## Strengths

- One folder per case with a stable naming rule, an archive with checksums, and ten years of history retrievable.
- The editorial role became a real server: queue directories, a tracker, scripts that drive the journal system, and an incident that produced an invariant and a tool.
- Skills cover every editor decision type except accept, and they infer the journal from the path.
- Confidential inputs (referee cover letters versus author-facing reports) are distinguished in the skill text.

## Weaknesses and limitations of the ad-hoc workflow

- **No outcome record**: what the editor decided, whether the report was used, and how long it took are absent; the archive is inputs and outputs only.
- **Duplicated and drifting prompts**: `prompt.txt` equals `prompt-03.txt`; `referee-review-support/SKILL (Elliott Ash's conflicted copy 2026-08-07).md` sits beside `SKILL.md`; `JLE-Editing/_recent_email_summary (alt 2026-06-16).json` and `code/em_proxy_register_reviewer (alt 2026-06-16).py` are sync duplicates.
- **Tracker staleness**: `JLE-tracker.md` last refreshed 2026-08-19 while `_pending/` holds 12 manuscripts; the four `_active/under-review-N` queues are empty, so the queue scheme and the tracker disagree about where work is.
- **Strays**: `EJ-Guest-Issue/pending_assignments.xls.bak`, `editor-support-dossier-workspace/iteration-1` left in the skills folder, W-9 tax forms and an unrelated paper in `_misc/`.
- **Credentials beside data**: `JLE-Editing/.env` and `EJ-Guest-Issue/.env` live in the case tree; `openreview-ac` reads `~/alfred/.env`.
- **Decisions in a person's head**: which cases get the skill and which get a bare `report.txt` follows no written rule.

## What Relay would have to support here

- **A case object with a deadline, a role and a venue**, created from the invitation email, that owns its folder and moves to done with the recommendation and, later, the editor's outcome.
- **Skill routing by case type**: referee, PC, grant, editor-decision, AC, each with its skill or an explicit "no skill yet" so the 18 bare `report.txt` cases stop being invisible.
- **Confidential-by-default panes**: manuscripts, reports and `.env` files never leave the case; no board card may quote them; credentials move out of the tree.
- **Levels and pairwise verdicts** as the verification mode: the recommendation scale, agreement with other referees when their reports arrive, and the editor-support evals as the template for skill QA.
- **Browser automation with screenshot evidence** attached to the editorial action, replacing the 117 loose files in `_active/screenshots/`.
- **A tracker that is generated from the queue folders**, not hand-refreshed, so it cannot be five weeks behind.
