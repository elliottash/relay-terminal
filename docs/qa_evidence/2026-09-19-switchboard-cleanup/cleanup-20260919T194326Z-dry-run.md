# Switchboard cleanup c-a5da11

- **Run**: c-a5da11 (dry run, nothing written)
- **Finished**: 2026-09-19T19:51:38+00:00 after 492.9 s, outcome `done`
- **Model**: glm-5.3
- **Cards**: 254 before, 255 after
- **Writes**: 0, proposed 21
- **Scope**: the whole board

## Sections
- columns: `inbox, discussing, ready, in-progress, waiting, needs-qa, done` → `inbox, discussing, ready, in-progress, waiting, needs-qa, deferred, done`
- tabs: `features, bugs, design, marketing, planning, deferred, done` → `features, bugs, design, marketing, planning, deferred, done`

## Changes

| # | Action | Card | Summary | File |
|---|---|---|---|---|
| 1 | merges | #BTYE | *(proposed)* merge cards — into: BTYE; cards: ["DJX7"]; reason: Same fault filed twice on 2026-09-19: land.py's name gate collapses a moved file into one rename line; #BTYE carries the landed fix (de3510f, --no-renames in al… | `` |
| 2 | merges | #4WHD | *(proposed)* merge cards — into: 4WHD; cards: ["KH72"]; reason: One request — the owner's 2026-09-17 model-roles ask — recorded as a spec card (#KH72) and its implementation card (#4WHD); #KH72's own "Implemented" section de… | `` |
| 3 | move | #HG7K | *(proposed)* move card — status: needs-qa-llm; reason: All six tasks are done including the deploy and the QA-evidence task; commit 6e83ee9 and the evidence folder (fresh-install, streamed ask, keys modal, allowance… | `` |
| 4 | move | #0VT4 | *(proposed)* move card — status: needs-qa-llm; reason: All 11 tasks are done (web pane view, Relay-to-Relay pane drawing, sphinxpad LAN install), implemented_by is stamped 2026-09-18, and both evidence folders exist… | `` |
| 5 | move | #3AZG | *(proposed)* move card — status: ready; reason: The ask is unfulfilled and unowned: no tag after v0.1.0-beta.2 exists, and beta.2 does not contain the in-Relay /join work (no JoinDialog in its src/, JQ7R's co… | `` |
| 6 | move | #SFZC | *(proposed)* move card — status: inbox; tab: bugs; reason: Mis-filed by the intake fault #5G43 reports (new cards landing in-progress): no assignee, commits, tasks or work — the owner only reworded the issue today. It i… | `` |
| 7 | move | #5PY9 | *(proposed)* move card — tab: bugs; reason: A fault report ("81% left after /new … that can't be right"), so it belongs in the bugs tab with the other bug cards, as the owner has been doing himself; statu… | `` |
| 8 | update | #EC58 | *(proposed)* update card — fields: labels | `` |
| 9 | update | #5G43 | *(proposed)* update card — fields: labels | `` |
| 10 | update | #SFZC | *(proposed)* update card — fields: labels | `` |
| 11 | update | #5PY9 | *(proposed)* update card — fields: labels | `` |
| 12 | update | #Y2JW | *(proposed)* update card — fields: labels | `` |
| 13 | update | #T7AH | *(proposed)* update card — fields: labels | `` |
| 14 | update | #9K5H | *(proposed)* update card — fields: labels | `` |
| 15 | update | #20XA | *(proposed)* update card — fields: labels | `` |
| 16 | update | #265N | *(proposed)* update card — fields: labels | `` |
| 17 | update | #CVHT | *(proposed)* update card — fields: labels | `` |
| 18 | update | #VZ69 | *(proposed)* update card — fields: labels | `` |
| 19 | update | #QG60 | *(proposed)* update card — fields: labels | `` |
| 20 | update | #5AWD | *(proposed)* update card — fields: labels | `` |
| 21 | comment | #72NR | *(proposed)* comment — kind: note | `` |

## What the agent said

I'll work through this in order: roster, reads, then dry-run writes. Starting with the list and the first batch of cards I suspect need attention.

Roster matches. Now reading the cards I may touch — first the suspected duplicate pair (land.py name gate) and the in-progress cards.



Now the second read batch — the inbox cards and their suspected neighbours — plus read-only git checks for the in-progress cards.

Good progress. Key facts so far: the relay-free evidence folder exists, tags stop at `v0.1.0-beta.2`, and `#DJX7`/`#BTYE` is a confirmed same-day duplicate pair (BTYE's own resolution names DJX7). Now the next duplicate-candidate batch, plus tag dates for the release card.

The tag picture needs one decisive check (the beta.2 tag message claims the join work, but the card's commit sha isn't an ancestor — likely the history rewrite). Also reading the next batch: the two remaining in-progress cards, the folder-click discussing card vs its landed sibling, the poll-perf pair, and the queue-editing pair.

The beta.2 tag contains the *browser* meeting-code join (`remote/client.py`) but not the in-Relay `/join` work (`JoinDialog` in `src/`, commits not ancestors) — so the release card's ask is genuinely unfulfilled. `#AGN8`/`#9V1F` and `#KP4M`/`#V7QD` are deliberate follow-up pairs, not duplicates. Last read batch: the GitHub-sync trio and the remaining label-only cards.

All reads are done and reality is checked. Verifying the last evidence folder, then making the dry-run writes — merges first, then status moves, then labels.

Both merges recorded. Now the status moves:

Now the label fixes (13 cards with no labels) and the cross-reference note on `#72NR`:





**Dry run complete — 21 proposals recorded, nothing written.** 28 cards read in full; every card touched was read first.

- **Merged**:
  - `#DJX7 + #BTYE → #BTYE` — same-day double report of the same land.py name-gate fault; #BTYE carries the fix (`de3510f`, `--no-renames`), the test and the workaround, and its own resolution already names #DJX7 as the same fault.
  - `#KH72 + #4WHD → #4WHD` — one request (the owner's 2026-09-17 model-roles ask) split into a spec card and its implementation card; #KH72's "Implemented" section defers to #4WHD's file for the checklist, and both point at `docs/qa_evidence/2026-09-17-model-roles/`.
- **Status**:
  - `#HG7K in-progress → needs-qa-llm` (evidence `docs/qa_evidence/2026-09-18-relay-free/` — all 6 tasks done incl. deploy; "What is left" items are named follow-ons).
  - `#0VT4 in-progress → needs-qa-llm` (evidence `docs/qa_evidence/2026-09-18-web-pane-view/` — 11/11 tasks done, implementer stamped, sphinxpad LAN folder present).
  - `#3AZG in-progress → ready` — no tag after `v0.1.0-beta.2`, and beta.2 does not contain the in-Relay `/join` work (`JoinDialog` absent from its `src/`, #JQ7R's commits not ancestors); the card was filed straight into in-progress by the intake fault #5G43 reports and has no owner or work.
  - `#SFZC in-progress → inbox`, tab features → bugs — mis-filed by that same intake fault; no work on it; the owner only reworded the issue today.
  - `#5PY9` tab features → bugs (status unchanged).
- **Labels / ranks**: 13 empty label sets filled (`#5PY9, #Y2JW, #T7AH, #9K5H, #20XA, #5G43, #SFZC, #265N, #EC58, #CVHT, #VZ69, #QG60, #5AWD` — each got bug or feature plus one area label). No rank changes. One note proposed on `#72NR`: #JH5M landed the /proc and state.json halves; what remains is the on-screen idle poll rate and the `shellHistory()` re-parse.
- **Sections**: unchanged — every column is in use and none has stopped meaning anything.

**Left alone**, and why:

- `#KKYC` (discussing) — #0C7V landed "single click navigates", which arguably answers its question, but the card's acceptance is an owner decision and it is `waiting_on: owner`; not mine to close.
- `#5PY9` stays in **inbox**, not discussing — its plan carries one open question to the owner (the chip will read ~97% on a fresh convo, not 99–100%), but it does not block the fix.
- `#97EG` (live-run task open), `#3KB7` (6/10 tasks, evidence folder already appearing), `#GDQN` (engine landed, worker wiring open — a previous sweep set it in-progress deliberately, and #ZKR0 is correctly its separate GUI half) — all genuinely in progress.
- Suspected duplicates that are **not**: `#KP4M`/`#V7QD` (jobs vs subagents lines; one builds on the other by design), `#AGN8`/`#9V1F` (the second revises the first's Enter behaviour), `#EC58`/`#7FD3` (#7FD3 records a decision to stay separate), `#XZZB`/`#SP4N` (analysis vs implementation, both correctly in the QA lane), the four Relaying-line tweaks (`#4E13/#HQ2B/#RR0G/#4X53`) and the four command-not-found reports (`#T4JV/#W954/#N3WC/#8G17`) — same area, distinct asks.
- The ~30 cards on the changes/ tab whose only kind-label is `change` (or `theme`, `tests`): that is this board's own convention, not a missing label, so I filled only genuinely empty sets rather than re-labeling a convention.
- `#W8PC` (bug in discussing — waiting on the owner) and `#JM2M` (dropped, resolving fine under the status-filtered done tab): left as they are.

**Dry run complete — 21 proposals recorded, nothing written.** 28 cards read in full; every card touched was read first.

- **Merged**:
  - `#DJX7 + #BTYE → #BTYE` — same-day double report of the same land.py name-gate fault; #BTYE carries the fix (`de3510f`, `--no-renames`), the test, the workaround, and its own resolution already names #DJX7 as the same fault.
  - `#KH72 + #4WHD → #4WHD` — one request (the owner's 2026-09-17 model-roles ask) split into a spec card and its implementation card; #KH72's "Implemented" section defers to #4WHD's file for the checklist, and both share `docs/qa_evidence/2026-09-17-model-roles/`.
- **Status**:
  - `#HG7K in-progress → needs-qa-llm` (evidence `docs/qa_evidence/2026-09-18-relay-free/` — all 6 tasks done including deploy; the "What is left" items are named follow-ons, not open work).
  - `#0VT4 in-progress → needs-qa-llm` (evidence `docs/qa_evidence/2026-09-18-web-pane-view/` — 11/11 tasks done, implementer stamped, sphinxpad LAN folder present).
  - `#3AZG in-progress → ready` — no tag after `v0.1.0-beta.2`, and beta.2 does not contain the in-Relay `/join` work (`JoinDialog` absent from its `src/`, #JQ7R's commits not ancestors); the card was filed straight into in-progress by the intake fault #5G43 reports and has no owner or work.
  - `#SFZC in-progress → inbox`, tab features → bugs — mis-filed by that same intake fault; no work on it; the owner only reworded the issue today.
  - `#5PY9` tab features → bugs (status unchanged).
- **Labels / ranks**: 13 empty label sets filled — `#5PY9, #Y2JW, #T7AH, #9K5H, #20XA, #5G43, #SFZC, #265N, #EC58, #CVHT, #VZ69, #QG60, #5AWD` (each got bug or feature plus one area label). No rank changes. One note proposed on `#72NR`: #JH5M landed the /proc and state.json halves, so that card now stands for the on-screen idle poll rate and the `shellHistory()` re-parse.
- **Sections**: unchanged — every column is in use and none has stopped meaning anything.

**Left alone**, and why:

- `#KKYC` (discussing) — #0C7V landed "single click navigates", which arguably answers its question, but the card's acceptance is an owner decision and it is `waiting_on: owner`.
- `#5PY9` stays in **inbox**, not discussing — its plan carries one open question to the owner (a fresh conversation reads ~97% left, not 99–100), but it does not block the fix.
- `#97EG` (live-run task open), `#3KB7` (6/10 tasks, evidence folder already appearing), `#GDQN` (engine landed, worker wiring open — a previous sweep set it in-progress deliberately, and #ZKR0 is correctly its separate GUI half) — all genuinely in progress.
- Suspected duplicates that are **not**: `#KP4M`/`#V7QD` (jobs vs subagents waiting lines; one builds on the other by design), `#AGN8`/`#9V1F` (the second revises the first's Enter behaviour), `#EC58`/`#7FD3` (#7FD3 records a decision to stay separate), `#XZZB`/`#SP4N` (analysis vs implementation, both correctly in the QA lane), the four Relaying-line tweaks (`#4E13/#HQ2B/#RR0G/#4X53`) and the four command-not-found reports (`#T4JV/#W954/#N3WC/#8G17`) — same area, distinct asks.
- The ~30 changes-tab cards whose only kind-label is `change` (or `theme`, `tests`): this board's own convention, not a missing label — I filled only genuinely empty sets rather than re-labeling a convention.
- `#W8PC` (bug in discussing — waiting on the owner) and `#JM2M` (dropped, resolving fine under the status-filtered done tab): left as they are.
