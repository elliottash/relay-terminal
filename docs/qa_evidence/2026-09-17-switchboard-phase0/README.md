# Switchboard phase 0 — implementer evidence (2026-09-17)

Produced by the implementer (Claude Opus 5, 1M context). **Not a QA verdict.**
Issue: `issues/features/needs_qa_llm/2026-09-17-switchboard-phase0.md`.

The migration was **not** run on this repository's `issues/` tree — other agents were editing it.
Everything below ran over a scratch copy of that tree (57 issue files, the state before this issue
file was added).

| File | What it is |
|---|---|
| `migrate-dry-run.txt` | `scripts/relay-board.py migrate` over the copy: per-file id, status and rank; nothing written |
| `migrate-apply.txt` | the same with `--apply` |
| `check.txt` | `scripts/relay-board.py check` on the migrated copy: 57 cards, 0 errors, 0 warnings |
| `verify-migration.py`, `verify-migration.txt` | body byte-fidelity, header-value survival, a PyYAML cross-check of every emitted front matter, and a determinism re-run |
| `BOARD.md`, `board.yaml` | the generated index and the config the migration creates |
| `example-migrated-card.md` | one migrated card in full (`issues/features/2026-09-17-voice-transcription.md`) |
| `example-card-with-tasks.md`, `example-thread.md` | a card and thread written by `board.py` alone (task markers with statuses, a dependency, a card link, a dropped item; four thread entries with sortable ids), `check` clean |
| `test-suite.txt` | `./scripts/test.sh`: 349 tests OK (273 before this change, 76 new in `tests/test_board.py`) |

Reproduce:

```bash
cp -r issues /tmp/board-copy
python3 scripts/relay-board.py --issues /tmp/board-copy migrate           # dry run
python3 scripts/relay-board.py --issues /tmp/board-copy migrate --apply
python3 scripts/relay-board.py --issues /tmp/board-copy check
python3 docs/qa_evidence/2026-09-17-switchboard-phase0/verify-migration.py issues /tmp/board-copy
```

Ids and ranks are derived from the file paths, so a re-run over the same tree reproduces the ids in
`migrate-dry-run.txt` exactly; adding or removing files changes the ranks (they are spaced per
category) but not the ids of the other cards.
