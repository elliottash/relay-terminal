# Issues

File-based tracker. One Markdown issue per file, named `YYYY-MM-DD-short-slug.md`.
Conventions follow the global `issue-tracking` skill (`~/.warp/skills/issue-tracking/SKILL.md`).

## Layout

| Folder | Holds |
|---|---|
| `feature_intake.txt`, `bug_intake*.txt` | Quick unsorted capture. Processed items are filed below and removed from intake. |
| `features/` | New capabilities |
| `changes/` | Changes to existing behavior, including bugs |
| `incorrect_assessments/` | Recorded wrong conclusions, created when first needed |

Open and in-progress issues sit at the top of their category folder. Resolved or
waiting issues move to a state subfolder inside the category, in the same commit
as the change that moved them:

| Subfolder | State | Meaning |
|---|---|---|
| `needs_qa_llm/` | `needs-qa-llm` | Landed, unchecked. A model QA session from a different model family checks it. |
| `needs_qa_human/` | `needs-qa-human` | Escalated QA that needs a person |
| `needs_labels/` | `needs-labels` | Waiting on labelled data |
| `needs_review/` | `needs-review` | Waiting on expert judgment of content |
| `needs_ab/` | `needs-ab` | Waiting on user evidence for a product decision |
| `done/` | `done` | Closed with a resolution section |

Only `needs_qa_*` are landing states. Labels, review and experiments can block
work that has not shipped. Never delete an issue; append rather than rewrite.

## Header fields

**Status**, **Component** (`agent`, `worker`, `router`, `gui`, `shell-integration`,
`providers`, `theme`), **Milestone** (`0.1-preview`, `desktop-alpha`), **Workstream**
(`agent`, `routing`, `terminal`, `providers`), **Acceptance evidence**, **Assignee**.
Record who implemented a change, including the model, so QA independence can be checked.

QA evidence lives under `docs/qa_evidence/YYYY-MM-DD-short-slug/`.
Commits that only add or triage issues use `issue: <short title>`.
