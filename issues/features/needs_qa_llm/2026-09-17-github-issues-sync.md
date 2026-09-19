---
id: GDQN
type: work
status: needs-qa-llm
labels: [feature]
component: [worker, gui]
milestone: post-mvp
workstream: switchboard
assignee: agent
implemented_by: glm/glm-5.3
rank: 3h
created: '2026-09-17'
acceptance: a shared work card and its GitHub issue stay in sync both ways (body, comments, status, labels) across edits on either side, with conflicts surfaced rather than lost
source: '`issues/feature_intake.txt`, 2026-09-17: "set up syncing the switchboar with github issues."'
links: {plans: [], commits: [f878b6a, a17a262, ae206932, ac7abe64, 9e628ce3, 1377a65a], evidence: [docs/qa_evidence/2026-09-19-github-issues-sync/], related: [ZKR0], github: null}
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
Built, wired and walked end to end; the acceptance run itself found and fixed one lost write.

- `f878b6a` (→ `ae206932` after the AGPL relicense rewrote the hashes): the engine,
  `backend/relay_core/forge_sync.py` + `forge_github.py`, provider-neutral, guarded for private
  cards, tested against the in-process `tests/fake_github.py`.
- `a17a262` (→ `ac7abe64`): the back-end review's fixes for the lost writes it found.
- `9e628ce3` (the 09-18/19 batch): **the worker wiring** the sweep said was missing —
  `forge_sync_plan` / `forge_sync_run` on `board_protocol.BoardCommands` (protocol 19.14), the
  thread, the busy guards, the scrubbed one-terminal-event errors, and the desktop-only marks in
  `remote/wire.py`.
- `1377a65a`: the acceptance walked through the worker messages found the listing trap — after a
  conflict leaves a card's baseline behind, the next quiet run read the unchanged (304) listing as
  "the issue equals the baseline" and pushed the local edit over the remote one. A per-card
  `current` marker in the state now gates that: a card whose baseline may lag gets its issue read
  again before anything is pushed. `ForgeSyncAcceptanceTests` walks both directions (body,
  comments, status, labels) and the whole conflict lifecycle through `dispatch`.

The person-facing surface (Sync action, plan-before-first-run, the conflict shown on the card with
keep-mine / keep-GitHub's) is #ZKR0, which follows this card. Nothing here points a real board at
a real repository: that is the owner's decision (`github: {repo: …}` in `board.yaml`).

## QA checklist
- [ ] `PYTHONPATH="$PWD/backend:$PWD/tests" python3 -m unittest tests.test_forge_sync
      tests.test_forge_github` → 147 tests OK, including
      `MergeTests.test_a_conflict_survives_a_quiet_sync_instead_of_pushing_over_it` (fails on the
      pre-`1377a65a` engine by pushing over the conflict).
- [ ] Same with `tests.test_board_protocol` → 145 OK, including both
      `ForgeSyncAcceptanceTests` tests: the walk covers body, a thread note out, a web comment in,
      labels, status both ways, the `links.github` stamp, `board_changed` after a pull, and a quiet
      run that writes nothing.
- [ ] The conflict lifecycle: the second sync re-reports the conflict in `forge_sync_done`
      (`conflicts` names the field and both versions), adds **no** second thread note, and
      `self.gh.writes` shows no write between the two syncs.
- [ ] Read `docs/qa_evidence/2026-09-19-github-issues-sync/README.md` and check its account of
      the trap against the code (`_sync`'s re-read condition, `_apply_card`'s `current` write,
      `_mark_lagging`).
- [ ] Confirm no test touches the network: the fake binds 127.0.0.1; `GH_TOKEN` in the protocol
      tests is the fake's own token.
- [ ] Skim `docs/GITHUB-SYNC.md` §3–§4 for truth after the change (the `current` marker, the
      one-real-read rule, the quiet-board conflict sentence).
