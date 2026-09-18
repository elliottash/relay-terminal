---
id: 23XM
type: work
status: needs-qa-llm
labels: [feature]
component: [worker]
milestone: desktop-alpha
workstream: switchboard
assignee: agent
implemented_by: Claude Opus 5 (1M context), 2026-09-17
rank: y9
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-switchboard-phase0/` (migration dry run and apply over a copy of the 57 real issue files, `check` output, body-fidelity and determinism verification, full test run); 76 new tests in `tests/test_board.py`, suite at 349 tests'
source: '`docs/SWITCHBOARD-DESIGN.md` section 10 phase 0 and owner decisions in section 12; `docs/TASKS-AND-MEMORY-DESIGN.md` section 9 (plans and memories are card types, private root)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Switchboard phase 0: card format, tasks, threads and `relay-board` tooling

## What landed

Format and tooling only — no agent tools (`board_*`), no UI, no note scanning, no GitHub sync, and
**the repository's own `issues/` tree was not migrated** (other agents are editing it; the owner runs
`scripts/relay-board.py migrate --apply` when the tree is quiet).

- `backend/relay_core/board.py`: card parse/write (YAML-subset front matter + Markdown body, byte-preserving
  round trip), card ids (4 Crockford-base32 characters, at least one letter, never all-digit), fractional
  ranks, `## Tasks` / `## Steps` checklist items with `<!-- t:xx -->` markers that still render as GitHub
  checkboxes, append-only threads in `issues/threads/<ID>.md` with sortable entry ids (`O_APPEND` + `flock`),
  card types `work`, `plan` and `memory`, the private root `issues/.private/`, atomic hash-checked writes,
  the `check` rules and the `BOARD.md` index. No model calls, no network.
- `scripts/relay-board.py`: `check [--fix] [--json] [--strict]`, `index`, `migrate [--apply]`.
- `docs/SWITCHBOARD-FORMAT.md`: the format reference, plus (section 7) the paragraph for the owner to paste
  into the global `issue-tracking` skill.
- `tests/test_board.py` with fixtures copied from six real issue files.

## Implementer evidence (not a QA verdict)

Over a scratch copy of the current `issues/` tree (57 issue files):

- `migrate` (dry run and apply): 57 cards, 0 unparsable — ready 14, in-progress 2, needs-qa-llm 39, done 2;
  would create `issues/board.yaml`, `issues/.gitignore`, `issues/threads/.gitkeep`, `.gitattributes`, `BOARD.md`.
- Every migrated body is byte-identical to the original minus its header block; every header value survives
  into the front matter; PyYAML reads every emitted front matter with the same values; a second run produces
  identical bytes.
- `check` on the migrated copy: 57 cards, 0 errors, 0 warnings.
- `./scripts/test.sh`: 349 tests OK (273 before, 76 new).

## QA checklist

1. **Round trip.** Read a card with `Card.parse` and write it back unchanged: bytes identical, including
   unusual front matter (comments, quoting). Change one field: only the front matter block is rewritten, in
   canonical order, and the body is untouched.
2. **Ids.** 200 generated ids are 4 characters, contain a letter, never contain I/L/O/U, and are never
   all-digit; `new_id(taken)` avoids collisions; `derived_id` is stable across runs.
3. **Ranks.** `rank_between` always returns a value strictly between its bounds, including 30 repeated
   inserts at the same spot and inserting before the first card; ranks never end in `0`; `initial_ranks(n)`
   is sorted and unique for n up to a few hundred.
4. **Task markers.** Tick a box in a text editor without touching the marker (`- [x] … <!-- t:a3
   s=in-progress -->`): the item reads as done and the marker is rewritten on the next write. Add a bare
   `- [ ] text` line: `check --fix` gives it an id and changes nothing else. Untick a `s=done` item: it is
   open again. Dropped items keep `~~strikethrough~~`. Rewriting the checklist leaves every other byte alone.
5. **Threads.** Two appends from two processes both survive; entry ids sort chronologically; a simulated
   union merge (`git merge-file --union`) of two branches that each appended keeps both entries with no
   conflict markers; `check` reports duplicate and out-of-order entries and `--fix` re-sorts.
6. **Check rules.** Each rule fires on a deliberately broken card and stays silent on a good one: folder vs
   status, duplicate id, unknown field, unknown type/status, bad task markers, `blocked_by` cycles, thread
   ordering, merge markers, private file tracked by git (`git add -f` a file under `issues/.private/`).
7. **Migration.** Re-run `scripts/relay-board.py migrate` over a fresh copy of `issues/`: same ids and ranks
   as the evidence files, nothing unparsable, bodies unchanged, `check` clean afterwards. Confirm a file
   without a header block is reported and left alone. Confirm the dry run writes nothing.
8. **Types and privacy.** A `plan` card in `planning/` with `## Steps`, a `memory` card in `memory/`, and a
   private card under `issues/.private/features/` all pass `check`; `index` hides private cards unless asked.
9. **No model calls or network.** Grep `board.py` and `relay-board.py`: no provider, no `requests`, no socket.
10. **Independence.** Implemented by Claude Opus 5; QA must come from a different model family.

## Open points for the owner

- The migration has not been run on this repository; the owner runs it when `issues/` is quiet, in a commit of
  its own (`issue: migrate tracker to board format`), and updates `issues/README.md` in the same commit.
- The global `issue-tracking` skill paragraph is in `docs/SWITCHBOARD-FORMAT.md` section 7 to paste; no file
  outside this repository was edited.
- Memory cards use `kind:` for `convention | fact | lesson | reference | preference` because `type:` names the
  card type (`work | plan | memory`); `docs/TASKS-AND-MEMORY-DESIGN.md` section 4.2 still calls it `type`.
