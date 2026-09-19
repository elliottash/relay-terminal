---
id: GDQN
type: work
status: in-progress
labels: [feature]
component: [worker, gui]
milestone: post-mvp
workstream: switchboard
rank: 3h
created: '2026-09-17'
acceptance: a shared work card and its GitHub issue stay in sync both ways (body, comments, status, labels) across edits on either side, with conflicts surfaced rather than lost
source: '`issues/feature_intake.txt`, 2026-09-17: "set up syncing the switchboar with github issues."'
links: {plans: [], commits: [f878b6a, 0d10c30], evidence: [], related: [], github: null}
---
# Two-way sync between the Switchboard and GitHub Issues

## Decisions (owner, 2026-09-17)
1. Two-way sync.
2. Only shared work cards sync. Private cards, plans and memories never do.
3. Mapping to be chosen sensibly (proposal below).
4. After the MVP.

## Proposed mapping
| Switchboard | GitHub |
|---|---|
| work card title and body | issue title and body |
| thread entries | issue comments (Relay-authored entries carry a hidden marker so they are not re-imported) |
| tab | label `tab:<name>` |
| status (inbox, discussing, ready, in-progress, needs-qa-*) | label `status:<name>`; `done` and `dropped` close the issue (reason completed / not planned) |
| labels | labels |
| assignee (person) | assignee when the person maps to a GitHub login |
| `## Tasks` items | task-list checkboxes in the body; items promoted to cards become sub-issues |
| card id `#K7Q2` | `relay-id` hidden marker in the body; `github: owner/repo#123` in card front matter |
| linked plan cards | a link in the body only (plans do not sync) |

See `docs/SWITCHBOARD-DESIGN.md` section 10 phase 3.

## Where it stands (2026-09-19 board sweep)

Not `ready` any more — half of it is built. `f878b6a` landed the two-way sync as a **headless
engine**: `backend/relay_core/forge_sync.py` and `forge_github.py`, 1150 lines of tests against an
in-process fake GitHub (`tests/fake_github.py`), with the message shapes in `docs/GITHUB-SYNC.md`.
`0d10c30` then fixed the lost writes a back-end review of it found.

What the acceptance still needs is the half that commit's own message names: **it is not wired to
the worker**, so nothing in Relay drives it yet and no conflict is surfaced to a person. The wiring
note is at the end of `docs/GITHUB-SYNC.md`. #ZKR0 is the Switchboard-side surface for the same
work and is still open.
