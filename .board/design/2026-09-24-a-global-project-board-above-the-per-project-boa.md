---
id: V3R3
type: work
status: discussing
labels: [feature, board, switchboard, research]
assignee: agent
implemented_by: anthropic/claude-fable-5-1 via claude-code
session: d149c662-381c-4406-8291-53d76af911bf
waiting_on: owner
priority: 2
rank: zzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: decision, primary: person, also: [ai-text], human: required, criteria: 'The owner reads docs/GLOBAL-PROJECT-BOARD-RESEARCH.md and answers the eight questions in its section 6; the ground-truth counts (38 Trello cards, 112 sheet rows, 88 project-notes folders, 77 matches) reproduce from the sources', sign_off: none, effort: medium, stakes: rework}
source: owner in Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [JN7X, 1QKM, GRT2], github: null}
---
# A global project board above the per-project Boards: criticality, time budgets, cross-project items (project-notes, Trello, Projects Dashboard)

## Issue
i want to create a kind of global project board, where i can track projects. i am thinking like trello. you can see how i track projects in my trell board (<private Trello URL>) and this project list:<private spreadsheet URL>
an assistant has creds for both. you can also look at ~/project-notes which was an aborted effort, which i would like to actually make work here. 

do deep research on other systems, for a global project management system that will then connect to all the project boards. 

so with that i can tag which projects are most critical, maybe set time budgets per project. any thing cross project goes here.

## Done means
The research is on disk and readable without this session: three sourced passes under `docs/research/global-project-board/` (products' portfolio layer; plain-text and git-native systems; prioritisation, budgets and academic paper pipelines) and a synthesis with a proposed design in `docs/GLOBAL-PROJECT-BOARD-RESEARCH.md`. The owner's three trackers were read through their APIs and files, not guessed at, and the named data sits in `relay-internal/planning/global-project-board-ground-truth.md`. The proposal names storage, the project record's fields, criticality, budgets, the two-way connection to per-project boards, one-time imports, phasing and traps, and ends in the questions the owner has to decide. Failure looks like: a design that copies cards upward or keeps the sheet as a second truth, numbers not traceable to a source, or a proposal that could not start phase 0 without another design pass.

## Discussion points
Synthesis and proposal: [docs/GLOBAL-PROJECT-BOARD-RESEARCH.md](../../docs/GLOBAL-PROJECT-BOARD-RESEARCH.md); source passes under [docs/research/global-project-board/](../../docs/research/global-project-board/); named tracker data in `relay-internal/planning/global-project-board-ground-truth.md`.

**Ground truth.** Trello "Work" is a to-do board (38 cards in TODAY / soon / Deadlined / soonish / Fun; 2 name a research project). The Google Sheet is the real registry (29 Working + 83 On Hold rows, stage 0–9, active 1/0.5/0, journal) and it rotted: Priority, My Turn and Delay Until are empty on every row, Last Meeting is 2018–2021 on the 8 rows that have it, and half the On Hold rows no longer follow the header. `~/project-notes` (88 READMEs, 86 with to-dos) is the best-structured record and was written in one session on 2026-03-08 and touched once since. 77 of 112 sheet rows match an project-notes folder; the union is ~120 papers. Relay's registry knows 10 software/admin projects and none of the papers. Each tracker died the same way: nothing read it.

**What the research agrees on.** The layer above a project is a thin wrapper with overlay fields, never a copy of cards (Linear, Shortcut, GitHub Projects); rollups are computed at read time, committed indexes drift (org, Dataview, Beads); status, health and progress are three fields (Linear, Asana); a dated update with a health pick is the staleness signal; portfolio priority is its own capped integer (Linear 2024, Basecamp Lineup, Doerr, Burkeman); a per-project weight ranks items without touching them (Taskwarrior); time budgets are hours/week against capacity and rot unless metered (Asana Workload, Toggl); a stuck-projects report is the cheapest health check (org-mode); nobody meters agent spend per project with a burn-down, and Relay can.

**Proposal.** The existing global Board (`~/.config/relay/switchboard/`, owner decision 2026-09-17) gains a `project` card type, one file per project in `projects/` (project-notes moves in), with kind, status, stage, criticality (= `priority`, +3 capped at five), health, budget {hours/week, agent $/month}, ball-in-court, next, deadline-with-owner, people, roots and links. Everything about the work is computed from the roots on this machine at read time. Four reports: stuck, stale, ball overdue, over budget. One-time importers for project-notes, the sheet and Trello. A `/weekly-review` skill is the reader project-notes never had. Phases: 0 record + imports (small, specified), 1 Projects tab in the Board pane (UI, own card), 2 budgets with actuals from Relay's usage accounting, 3 updates and review. Eight questions for the owner are in the thread and in §6.
