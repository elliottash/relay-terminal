# Issues

File-based tracker. One Markdown issue per file, named `YYYY-MM-DD-short-slug.md`.
Conventions follow the global `issue-tracking` skill (`~/.warp/skills/issue-tracking/SKILL.md`).

**QA lane:** implemented work waits in `*/needs_qa_llm/` for a QA session by a non-Claude model;
the current list is in [`docs/VALIDATION.md`](../docs/VALIDATION.md#the-qa-lane).

## Layout

What exists today (2026-09-17):

| Path | Holds |
|---|---|
| `feature_intake.txt`, `bug_intake.txt` | Quick unsorted capture. Processed items are filed below and removed from intake. |
| `features/` | New capabilities: open and in-progress issues at the top level |
| `features/needs_qa_llm/` | Implemented features waiting for model QA |
| `features/done/` | Closed features, each with a resolution section |
| `changes/` | Changes to existing behavior, including bugs. No open items at the top level today. |
| `changes/needs_qa_llm/` | Implemented changes waiting for model QA |

Created when first needed, not present yet: `changes/done/`, `incorrect_assessments/`
(recorded wrong conclusions), and the other state subfolders below.

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

**Status** (`open`, `in-progress`, or a state above), **Component** (`agent`, `worker`, `router`,
`gui`, `shell-integration`, `providers`, `theme`), **Milestone** (`0.1-preview`, `desktop-alpha`,
`cross-platform`), **Workstream** (`agent`, `routing`, `terminal`, `providers`), **Acceptance
evidence**, **Assignee**, **Source**. Record who implemented a change, including the model, so QA
independence can be checked.

QA evidence lives under `docs/qa_evidence/YYYY-MM-DD-short-slug/`. Implementer screenshots are
prefixed `implementer-`; they are not QA verdicts. Planned work across issues is summarized in
[`docs/ROADMAP.md`](../docs/ROADMAP.md).

## The body

The first `# ` heading is the title, and **`## Issue`** holds what the card is about in the words
of whoever asked for it — verbatim, never tidied. It was called `## Request` until 2026-09-18
(owner: "i'm not sure about 'request' there, let's call it issue"); both spellings are read, cards
already filed keep theirs, and anything that writes that section settles the card on `## Issue`.
The other sections (`## Tasks`, `## Decisions`, findings, the QA checklist and the verdict) are
described in [`docs/SWITCHBOARD-FORMAT.md`](../docs/SWITCHBOARD-FORMAT.md). In Relay, the title
and the issue text are edited on the card itself (`e`, or the Edit button).

Commits that only add or triage issues use `issue: <short title>`.
